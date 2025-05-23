{% include 'header.sql.jinja' %}

-- start query 1 in stream 0 using template query51.tpl and seed 1819994127
$web_v1 = (
select
  web_sales.ws_item_sk item_sk, date_dim.d_date as d_date,
  sum(sum(ws_sales_price))
      over (partition by web_sales.ws_item_sk order by date_dim.d_date rows between unbounded preceding and current row) cume_sales
from {{web_sales}} as web_sales
cross join
     {{date_dim}} as date_dim
where ws_sold_date_sk=d_date_sk
  and d_month_seq between 1200 and 1200+11
  and ws_item_sk is not NULL
group by web_sales.ws_item_sk, date_dim.d_date);

$store_v1 = (
select
  store_sales.ss_item_sk item_sk, date_dim.d_date as d_date,
  sum(sum(ss_sales_price))
      over (partition by store_sales.ss_item_sk order by date_dim.d_date rows between unbounded preceding and current row) cume_sales
from {{store_sales}} as store_sales
cross join
    {{date_dim}} as date_dim
where ss_sold_date_sk=d_date_sk
  and d_month_seq between 1200 and 1200+11
  and ss_item_sk is not NULL
group by store_sales.ss_item_sk, date_dim.d_date);

 select  *
from (select item_sk
     ,d_date
     ,web_sales
     ,store_sales
     ,max(web_sales)
         over (partition by item_sk order by d_date rows between unbounded preceding and current row) web_cumulative
     ,max(store_sales)
         over (partition by item_sk order by d_date rows between unbounded preceding and current row) store_cumulative
     from (select case when web.item_sk is not null then web.item_sk else store.item_sk end item_sk
                 ,case when web.d_date is not null then web.d_date else store.d_date end d_date
                 ,web.cume_sales web_sales
                 ,store.cume_sales store_sales
           from $web_v1 as web full outer join $store_v1 as store on (web.item_sk = store.item_sk
                                                          and web.d_date = store.d_date)
          )x )y
where web_cumulative > store_cumulative
order by item_sk
        ,d_date
limit 100;

-- end query 1 in stream 0 using template query51.tpl
