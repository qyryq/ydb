### Общие параметры для всех видов нагрузки {#run_options}

Имя | Описание | Значение по умолчанию
---|---|---
`--output <значение>` |  Имя файла, в котором будут сохранены результаты выполнения запросов. | `results.out`
`--iterations <значение>` | Количество выполнений каждого из запросов нагрузки. | `1`
`--json <имя>` | Имя файла, в котором будет сохранена статистика выполнения запросов в формате `json`. | Файл не сохраняется
`--ministat  <имя>` | Имя файла, в котором будет сохранена статистика выполнения запросов в формате `ministat`. | Файл не сохраняется
`--plan  <имя>` | Имя файла для сохранения плана запроса. Если задано, то сохраняются файлы `<имя>.<номер запроса>.explain` и `<имя>.<номер запроса>.<номер итерации>` с планами в нескольких форматах: `ast`, `json`, `svg` и `table`. | Планы не сохраняется.
`--query-prefix <префикс>` | Префикс запроса. Каждый префикс будет добавлен отдельной строкой в начало каждого запроса. Если нужно указать несколько префиксов, используйте параметр несколько раз. | По умолчанию не задан
`--retries` | Количество повторных попыток выполнения каждого запроса | `0`
`--include` | Номера или отрезки номеров запросов, которые нужно выполнить в рамках нагрузки. Указываются через запятую, например `1,2,4-6`. | Все запросы
`--exclude` | Номера или отрезки номеров запросов, которые нужно исключить в рамках нагрузки. Указываются через запятую, например `1,2,4-6`. |
`--executer` | Механизм выполнения запросов, доступные значения: `scan`, `generic`. | `generic`
`--verbose` или `-v` | Выводить больше информации на экран в процессе выполнения запросов. |
