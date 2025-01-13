#!/usr/bin/env python
# -*- coding: utf-8 -*-

import pytest
from hamcrest import assert_that, raises

from ydb.tests.library.sqs.test_base import KikimrSqsTestBase, IS_FIFO_PARAMS, TABLES_FORMAT_PARAMS


class TestQueueTags(KikimrSqsTestBase):
    @classmethod
    def _setup_config_generator(cls):
        config_generator = super(TestQueueTags, cls)._setup_config_generator()
        return config_generator

    @pytest.mark.parametrize(**IS_FIFO_PARAMS)
    @pytest.mark.parametrize(**TABLES_FORMAT_PARAMS)
    def test_list_queue_tags(self, is_fifo, tables_format):
        self._init_with_params(is_fifo, tables_format)

        queue_url = self._create_queue_and_assert(self.queue_name, is_fifo=is_fifo, use_http=True)

        def get_tags():
            return self._sqs_api.list_queue_tags(queue_url)

        assert get_tags() == {}

    @pytest.mark.parametrize(**IS_FIFO_PARAMS)
    @pytest.mark.parametrize(**TABLES_FORMAT_PARAMS)
    def test_tag_queue(self, is_fifo, tables_format):
        self._init_with_params(is_fifo, tables_format)

        queue_url = self._create_queue_and_assert(self.queue_name, is_fifo=is_fifo, use_http=True)

        def add_tags(tags):
            return self._sqs_api.tag_queue(queue_url, tags)

        def get_tags():
            return self._sqs_api.list_queue_tags(queue_url)

        assert get_tags() == {}

        add_tags({})
        assert get_tags() == {}

        add_tags({'key1': 'value0'})
        assert get_tags() == {'key1': 'value0'}

        # Change an existing tag value:
        add_tags({'key1': 'value1'})
        assert get_tags() == {'key1': 'value1'}

        # Adding a new tag without mentioning an existing tag should not delete the latter one:
        add_tags({'key2': 'value2'})
        assert get_tags() == {'key1': 'value1', 'key2': 'value2'}

        # Add multiple tags:
        add_tags({'key3': 'value3', 'key4': 'value0'})
        assert get_tags() == {'key1': 'value1', 'key2': 'value2', 'key3': 'value3', 'key4': 'value0'}

        # Add a new tag and change an existing tag:
        add_tags({'key4': 'value4', 'key5': 'value5'})
        assert get_tags() == {'key1': 'value1', 'key2': 'value2', 'key3': 'value3', 'key4': 'value4', 'key5': 'value5'}

    @pytest.mark.parametrize(**IS_FIFO_PARAMS)
    @pytest.mark.parametrize(**TABLES_FORMAT_PARAMS)
    def test_invalid_tag_queue(self, is_fifo, tables_format):
        # Test invalid key/values

        self._init_with_params(is_fifo, tables_format)

        queue_url = self._create_queue_and_assert(self.queue_name, is_fifo=is_fifo, use_http=True)

        def add_tags(tags):
            return self._sqs_api.tag_queue(queue_url, tags)

        def get_tags():
            return self._sqs_api.list_queue_tags(queue_url)

        def check(tags, pattern):
            assert_that(lambda: add_tags(tags), raises(RuntimeError, pattern=pattern))
            assert get_tags() == {}

        check({'': ''}, 'Tag key must not be empty.')

        # Delete this line:
        self._sqs_api._SqsHttpApi__raise_on_error = False
        # Replace with proper checks like this:

        add_tags({'': ''})
        assert get_tags() == {}

        add_tags({'a': ''})
        assert get_tags() == {}

        add_tags({'': 'a'})
        assert get_tags() == {}

        add_tags({'^': 'a'})
        assert get_tags() == {}

        add_tags({'4': 'a'})
        assert get_tags() == {}

        add_tags({'a': '^'})
        assert get_tags() == {}

        add_tags({'a'*100: 'a'})
        assert get_tags() == {}

        add_tags({'a': 'a'*100})
        assert get_tags() == {}

        # Too many tags:
        add_tags({f'k{i}': 'v' for i in range(80)})
        assert get_tags() == {}

    @pytest.mark.parametrize(**IS_FIFO_PARAMS)
    @pytest.mark.parametrize(**TABLES_FORMAT_PARAMS)
    def test_untag_queue(self, is_fifo, tables_format):
        self._init_with_params(is_fifo, tables_format)

        queue_url = self._create_queue_and_assert(self.queue_name, is_fifo=is_fifo, use_http=True)

        def add_tags(tags):
            return self._sqs_api.tag_queue(queue_url, tags)

        def remove_tags(keys):
            return self._sqs_api.untag_queue(queue_url, keys)

        def get_tags():
            return self._sqs_api.list_queue_tags(queue_url)

        # Removing unknown tags is OK:
        remove_tags(['key0'])

        add_tags({'key1': 'value1', 'key2': 'value2', 'key3': 'value3'})

        remove_tags(['key3'])
        assert get_tags() == {'key1': 'value1', 'key2': 'value2'}

        remove_tags(['key1', 'key2'])
        assert get_tags() == {}
