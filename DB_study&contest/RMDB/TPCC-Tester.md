## 可执行文件路径
生成了一个可执行文件，路径在：

```
/tmp/tpcc-target/release/tpcc-tester
```

这是一个 Rust 项目。编译完成后，不需要每次打开 Rust 工程，也不需要重新编译，直接运行这个二进制文件即可。

## 帮助命令
可以用下面的命令查看它的帮助：

```
/tmp/tpcc-target/release/tpcc-tester --help
```

## 使用指南
整个过程分为三个步骤。

### 第一步：启动数据库服务端

启动项目：

```
/home/ubuntu/projects/T2026106119910626-33-main/build/bin/rmdb
```

注意：数据库都默认都使用 8765 端口，所以需要先启动一个，测试完毕后再测试另一个。

### 第二步：运行 TPCC-Tester

TPCC-Tester 连接数据库后，可以执行：

- 初始化：创建 TPC-C 所需的表和索引，装载测试数据。
- 事务测试：模拟仓库订单、付款、订单状态查询等业务。
- 性能统计：统计 TPS、事务成功数、失败数、重试数等。
- 一致性检查：检查测试完成后数据库中的业务数据是否符合预期。

这里：

| 参数                  | 作用            |
| ------------------- | ------------- |
| `--legacy-protocol` | 使用旧版数据库通信协议   |
| `--init`            | 初始化 TPC-C 数据  |
| `--reset`           | 重置测试数据        |
| `--csv-path`        | 指定 CSV 数据文件目录 |
| `--seed`            | 指定随机数据种子      |
| `RUST_LOG`          | 控制日志输出级别      |
#### 初始化：
```bash
(base) ubuntu@MOSFET:~/projects/TPCC-Tester-main$ RUST_LOG=tpcc_tester=info /tmp/tpcc-target/release/tpcc-tester --init --reset --csv-path /tmp/tpcc-test --seed 42
```

**必须在该目录下运行才行！**

**注意：**
**`--init --reset` 只在初始化或重新建立 TPC-C 测试环境时执行一次。** 正常情况下，每次进行性能测试，都不需要重新建表和导入数据。

**`create_tables.sql` 就是 TPCC-Tester 用来创建 TPC-C 数据库表结构的 SQL 脚本。** 它包含创建 TPC-C 九张核心业务表的 `CREATE TABLE` 语句。

而且，它是可以重复使用的。只要数据库支持其中的 SQL 语法，这份脚本就可以用于初始化不同的数据库实例。

**create_tables.sql 到底是什么？**

它位于：

```
/home/ubuntu/projects/TPCC-Tester-main/sql/create_tables.sql
```

主要负责创建以下九张表：

| 表名         | 作用     | 初始数据量（1 Warehouse） |
| ---------- | ------ | ------------------ |
| WAREHOUSE  | 仓库信息   | 1                  |
| DISTRICT   | 区域信息   | 10                 |
| CUSTOMER   | 客户信息   | 10,000             |
| HISTORY    | 客户交易历史 | 10,000             |
| ITEM       | 商品信息   | 1,000              |
| STOCK      | 库存信息   | 1,000              |
| ORDERS     | 订单信息   | 10,000             |
| NEW_ORDER  | 待处理订单  | 3,000              |
| ORDER_LINE | 订单明细   | 50,000             |

有的项目使用 Wire v3 协议，因此不需要添加 `--legacy-protocol`。

#### 执行测试
```bash
(base) ubuntu@MOSFET:~/projects/TPCC-Tester-main$ 

RUST_LOG=tpcc_tester=info /tmp/tpcc-target/release/tpcc-tester --benchmark --threads 1 --transactions 100
```

#### 输出
```
(base) ubuntu@MOSFET:~/projects/TPCC-Tester-main$ RUST_LOG=tpcc_tester=info /tmp/tpcc-target/release/tpcc-tester --benchmark --threads 1 --transactions 100
2026-11-05T10:01:22.623513Z  INFO tpcc_tester: 连接 RMDB: 127.0.0.1:8765 (Wire Protocol v3) ...
2026-11-05T10:01:22.624362Z  INFO tpcc_tester: RMDB 连接正常
2026-11-05T10:01:22.624402Z  INFO tpcc_tester: 启动 TPC-C 基准测试...
2026-11-05T10:01:24.099417Z  INFO tpcc_tester::executor: benchmark complete: 100 successes, 0 failed logical transactions, 0 retries

============================================================
LOCAL SIMPLIFIED TPC-C FUNCTIONAL/PERFORMANCE TEST (not official tpmC)
============================================================
Configuration: seed=20261103, 1 threads, 100 transactions/thread
Scale: 1 warehouses, 10 districts/warehouse, 1000 customers/district, 1000 items, 1000 orders/district, 5 lines/order, 300 initial new orders/district
Read-Write Ratio: 0.92

Performance:
  Total Transactions:        100
  Successful:                100
  Failed logical txns:         0
  Retry attempts:              0
  Success Rate:          100.00%
  Total Duration:          1.47 seconds
  Average Response:       14.74 ms
  Throughput:             67.80 TPS
  New-Order/min:         1789.84

Transaction Mix:
      NewOrder:     44 ( 44.0%)  P50=20.7ms P95=29.6ms P99=30.2ms
       Payment:     48 ( 48.0%)  P50=7.3ms P95=10.3ms P99=10.9ms
      Delivery:      3 (  3.0%)  P50=16.2ms P95=16.5ms P99=16.5ms
   OrderStatus:      2 (  2.0%)  P50=4.3ms P95=4.3ms P99=4.3ms
    StockLevel:      3 (  3.0%)  P50=44.1ms P95=44.6ms P99=44.6ms
============================================================
```


### 第三步：查看测试结果
#### 每轮测试完成后检查数据

运行：

```
/tmp/tpcc-target/release/tpcc-tester --stats
```

查看九张表的记录数量。

再运行：

```
/tmp/tpcc-target/release/tpcc-tester --check
```

检查 TPC-C 数据一致性。

#### 运行结果

```
(base) ubuntu@MOSFET:~/projects/TPCC-Tester-main$ /tmp/tpcc-target/release/tpcc-tester --stats
```

```
2026-11-05T10:02:29.126121Z  INFO tpcc_tester: 连接 RMDB: 127.0.0.1:8765 (Wire Protocol v3) ...
2026-11-05T10:02:29.127090Z  INFO tpcc_tester: RMDB 连接正常
2026-11-05T10:02:29.127129Z  INFO tpcc_tester::checker: ========================================
2026-11-05T10:02:29.127201Z  INFO tpcc_tester::checker:        数据库表行数统计
2026-11-05T10:02:29.127236Z  INFO tpcc_tester::checker: ========================================
2026-11-05T10:02:29.127560Z  INFO tpcc_tester::checker:         warehouse:            1
2026-11-05T10:02:29.127839Z  INFO tpcc_tester::checker:          district:           10
2026-11-05T10:02:29.129161Z  INFO tpcc_tester::checker:          customer:        10000
2026-11-05T10:02:29.129793Z  INFO tpcc_tester::checker:              item:         1000
2026-11-05T10:02:29.130181Z  INFO tpcc_tester::checker:             stock:         1000
2026-11-05T10:02:29.130788Z  INFO tpcc_tester::checker:            orders:        10088
2026-11-05T10:02:29.132687Z  INFO tpcc_tester::checker:        order_line:        50846
2026-11-05T10:02:29.133041Z  INFO tpcc_tester::checker:        new_orders:         3028
2026-11-05T10:02:29.133632Z  INFO tpcc_tester::checker:           history:        10096
2026-11-05T10:02:29.133651Z  INFO tpcc_tester::checker: ========================================
```

```
(base) ubuntu@MOSFET:~/projects/TPCC-Tester-main$ /tmp/tpcc-target/release/tpcc-tester --check
```

```
2026-11-05T10:02:40.185506Z  INFO tpcc_tester: 连接 RMDB: 127.0.0.1:8765 (Wire Protocol v3) ...
2026-11-05T10:02:40.186647Z  INFO tpcc_tester: RMDB 连接正常
2026-11-05T10:02:40.186683Z  INFO tpcc_tester::checker: ========================================
2026-11-05T10:02:40.186687Z  INFO tpcc_tester::checker:        TPC-C 一致性检查
2026-11-05T10:02:40.186689Z  INFO tpcc_tester::checker: ========================================
2026-11-05T10:02:40.186691Z  INFO tpcc_tester::checker: [检查 1/4] 表行数检查
2026-11-05T10:02:40.187010Z  INFO tpcc_tester::checker:         warehouse: 1/1 PASS
2026-11-05T10:02:40.187335Z  INFO tpcc_tester::checker:          district: 10/10 PASS
2026-11-05T10:02:40.187702Z  INFO tpcc_tester::checker:              item: 1000/1000 PASS
2026-11-05T10:02:40.188804Z  INFO tpcc_tester::checker:          customer: 10000/10000 PASS
2026-11-05T10:02:40.189177Z  INFO tpcc_tester::checker:             stock: 1000/1000 PASS
2026-11-05T10:02:40.189745Z  INFO tpcc_tester::checker:            orders: 10088/10000 PASS
2026-11-05T10:02:40.191614Z  INFO tpcc_tester::checker:        order_line: 50846/50000 PASS
2026-11-05T10:02:40.191974Z  INFO tpcc_tester::checker:        new_orders: 3028/3000 PASS
2026-11-05T10:02:40.192494Z  INFO tpcc_tester::checker:           history: 10096/10000 PASS
2026-11-05T10:02:40.192515Z  INFO tpcc_tester::checker: [检查 2/4] District-Order 一致性 (d_next_o_id - 1 == MAX(o_id))
2026-11-05T10:02:40.207482Z  INFO tpcc_tester::checker:   District-Order 一致性检查 PASS
2026-11-05T10:02:40.207506Z  INFO tpcc_tester::checker: [检查 3/4] NewOrder 连续性 (COUNT == MAX - MIN + 1)
2026-11-05T10:02:40.222600Z  INFO tpcc_tester::checker:   NewOrder 连续性检查 PASS
2026-11-05T10:02:40.222626Z  INFO tpcc_tester::checker: [检查 4/4] OrderLine 一致性 (SUM(o_ol_cnt) == COUNT(ol_o_id))
2026-11-05T10:02:40.286947Z  INFO tpcc_tester::checker:   OrderLine 一致性检查 PASS
2026-11-05T10:02:40.286980Z  INFO tpcc_tester::checker: ========================================
2026-11-05T10:02:40.286984Z  INFO tpcc_tester::checker:   所有一致性检查通过
2026-11-05T10:02:40.286987Z  INFO tpcc_tester::checker: ========================================
```
重点关注：

- TPS：每秒成功执行的事务数量。
- 成功事务数。
- 失败事务数。
- 事务重试次数。
- 一致性检查结果。

##  加压测试？

TPCC-Tester 的压力主要可以从三个维度增加：

| 参数维度            | 增加后的效果        |
| --------------- | ------------- |
| Warehouse 数量    | 增加数据库测试数据规模   |
| Threads 数量      | 增加并发客户端数量     |
| Transactions 数量 | 延长测试时间、增加事务总量 |

例如，可以设计以下测试梯度：

| 测试等级 | Warehouse | Threads | 事务数   |
| ---- | --------- | ------- | ----- |
| 小规模  | 1         | 1       | 10    |
| 初级   | 1         | 2       | 100   |
| 中级   | 2         | 4       | 1000  |
| 高级   | 5         | 8       | 5000  |
| 压力测试 | 10        | 16      | 10000 |

具体参数名称和支持范围需要以当前版本 `--help` 为准。