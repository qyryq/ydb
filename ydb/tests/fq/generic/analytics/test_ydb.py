import logging
import pytest

import ydb.public.api.protos.draft.fq_pb2 as fq
import ydb.public.api.protos.ydb_value_pb2 as ydb
from ydb.tests.tools.fq_runner.kikimr_utils import yq_v2

from ydb.tests.tools.fq_runner.fq_client import FederatedQueryClient
from ydb.tests.fq.generic.utils.settings import Settings
from ydb.library.yql.providers.generic.connector.tests.utils.one_time_waiter import OneTimeWaiter
from yql.essentials.providers.common.proto.gateways_config_pb2 import EGenericDataSourceKind
import conftest


one_time_waiter = OneTimeWaiter(
    data_source_kind=EGenericDataSourceKind.YDB,
    docker_compose_file_path=conftest.docker_compose_file_path,
    expected_tables=["simple_table", "dummy_table"],
)


class TestYdb:
    @yq_v2
    @pytest.mark.parametrize("fq_client", [{"folder_id": "my_folder"}], indirect=True)
    @pytest.mark.parametrize(
        "mvp_external_ydb_endpoint", [{"endpoint": "tests-fq-generic-analytics-ydb:2136"}], indirect=True
    )
    def test_simple(self, fq_client: FederatedQueryClient, settings: Settings):
        table_name = 'simple_table'
        conn_name = f'conn_{table_name}'
        query_name = f'query_{table_name}'

        one_time_waiter.wait()

        fq_client.create_ydb_connection(
            name=conn_name,
            database_id=settings.ydb.dbname,
        )

        sql = fR'''
            SELECT *
            FROM {conn_name}.{table_name};
            '''

        query_id = fq_client.create_query(query_name, sql, type=fq.QueryContent.QueryType.ANALYTICS).result.query_id
        fq_client.wait_query_status(query_id, fq.QueryMeta.COMPLETED)

        data = fq_client.get_result_data(query_id)
        result_set = data.result.result_set
        logging.debug(str(result_set))
        assert len(result_set.columns) == 1
        assert result_set.columns[0].name == "number"
        assert result_set.columns[0].type == ydb.Type(
            optional_type=ydb.OptionalType(item=ydb.Type(type_id=ydb.Type.INT32))
        )
        assert len(result_set.rows) == 3
        assert result_set.rows[0].items[0].int32_value == 1
        assert result_set.rows[1].items[0].int32_value == 2
        assert result_set.rows[2].items[0].int32_value == 3
