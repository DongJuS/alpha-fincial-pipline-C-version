#![forbid(unsafe_code)]

use fred::prelude::*;
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use sqlx::postgres::PgPoolOptions;
use std::env;
use std::fs;
use std::path::PathBuf;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};
use thiserror::Error;
use tokio::sync::{Barrier, Semaphore};

const DEFAULT_REDIS_URL: &str = "redis://127.0.0.1:56379/0";
const DEFAULT_POSTGRES_URL: &str =
    "postgresql://alpha_test:alpha_test_only@127.0.0.1:55432/alpha_test";

#[derive(Debug, Error)]
enum AdapterError {
    #[error("invalid arguments: {0}")]
    Arguments(String),
    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),
    #[error("fixture JSON error: {0}")]
    Json(#[from] serde_json::Error),
    #[error("Redis error: {0}")]
    Redis(#[from] fred::error::Error),
    #[error("PostgreSQL error: {0}")]
    Postgres(#[from] sqlx::Error),
    #[error("task failed: {0}")]
    Task(#[from] tokio::task::JoinError),
    #[error("benchmark evidence is unsupported: {0}")]
    Evidence(String),
}

#[derive(Clone, Debug, Deserialize)]
struct Fixture {
    contract: Contract,
    cases: Cases,
}

#[derive(Clone, Debug, Deserialize)]
struct Contract {
    pipeline_depth: usize,
    queue_depth: usize,
    retry_policy: String,
    saturation_policy: String,
    service_config_sha256: String,
    schema_sha256: String,
}

#[derive(Clone, Debug, Deserialize)]
struct Cases {
    #[serde(rename = "redis-hot-path")]
    redis_hot_path: Case,
    #[serde(rename = "db-read-write")]
    db_read_write: Case,
}

#[derive(Clone, Debug, Deserialize)]
struct Case {
    operations: Vec<Operation>,
    repeat: usize,
    connection_count: usize,
    pipeline_depth: usize,
    terminal: Value,
    terminal_sha256: String,
    timeout_ms: u64,
}

#[derive(Clone, Debug, Deserialize)]
struct Operation {
    id: String,
    op: String,
    value: Option<String>,
    expected: Option<Value>,
    strategy_id: Option<String>,
    account_scope: Option<String>,
    quantity: Option<i64>,
    current_price: Option<i64>,
}

#[derive(Debug)]
struct Args {
    fixture: PathBuf,
    case: String,
    concurrency: usize,
    trial: usize,
    namespace: String,
}

#[derive(Debug, Serialize)]
struct Output {
    elapsed_ms: f64,
    fixture_sha256: String,
    completed_tokens: Vec<String>,
    operation_latency_ns: Vec<u64>,
    terminal: Value,
    result_sha256: String,
    errors: u64,
    dropped: u64,
    configuration: Configuration,
    resources: Resources,
    build: Build,
}

#[derive(Debug, Serialize)]
struct Configuration {
    concurrency: usize,
    operation_count: usize,
    pipeline_depth: usize,
    timeout_ms: u64,
    event_loop_mode: &'static str,
    worker_count: usize,
    queue_depth: usize,
    connection_count: usize,
    retry_policy: String,
    saturation_policy: String,
    service_config_sha256: String,
    schema_sha256: String,
}

#[derive(Debug, Serialize)]
struct Resources {
    peak_rss_bytes: u64,
    cpu_time_ms: f64,
}

#[derive(Debug, Serialize)]
struct Build {
    runtime: &'static str,
    compiler: &'static str,
    flags: &'static str,
    dependencies: Value,
}

fn parse_args(values: impl Iterator<Item = String>) -> Result<Args, AdapterError> {
    let mut fixture = None;
    let mut case = None;
    let mut concurrency = None;
    let mut trial = None;
    let mut namespace = None;
    let mut values = values.skip(1);
    while let Some(flag) = values.next() {
        let value = values
            .next()
            .ok_or_else(|| AdapterError::Arguments(format!("missing value for {flag}")))?;
        match flag.as_str() {
            "--fixture" => fixture = Some(PathBuf::from(value)),
            "--case" => case = Some(value),
            "--concurrency" => concurrency = Some(parse_usize(&value, "concurrency")?),
            "--trial" => trial = Some(parse_usize(&value, "trial")?),
            "--namespace" => namespace = Some(value),
            _ => return Err(AdapterError::Arguments(format!("unknown flag {flag}"))),
        }
    }
    let case = case.ok_or_else(|| AdapterError::Arguments("--case is required".into()))?;
    if !matches!(case.as_str(), "redis-hot-path" | "db-read-write") {
        return Err(AdapterError::Arguments("unsupported case".into()));
    }
    let concurrency = concurrency
        .filter(|value| matches!(value, 1 | 8 | 32))
        .ok_or_else(|| AdapterError::Arguments("concurrency must be 1, 8, or 32".into()))?;
    Ok(Args {
        fixture: fixture.ok_or_else(|| AdapterError::Arguments("--fixture is required".into()))?,
        case,
        concurrency,
        trial: trial.ok_or_else(|| AdapterError::Arguments("--trial is required".into()))?,
        namespace: namespace
            .filter(|value| !value.is_empty())
            .ok_or_else(|| AdapterError::Arguments("--namespace is required".into()))?,
    })
}

fn parse_usize(value: &str, name: &str) -> Result<usize, AdapterError> {
    value
        .parse()
        .map_err(|_| AdapterError::Arguments(format!("{name} must be a nonnegative integer")))
}

fn distribute(total: usize, workers: usize) -> Vec<usize> {
    let quotient = total / workers;
    let remainder = total % workers;
    (0..workers)
        .map(|index| quotient + usize::from(index < remainder))
        .collect()
}

fn required<'a>(value: &'a Option<String>, field: &str) -> Result<&'a str, AdapterError> {
    value
        .as_deref()
        .ok_or_else(|| AdapterError::Evidence(format!("operation missing {field}")))
}

fn required_i64(value: Option<i64>, field: &str) -> Result<i64, AdapterError> {
    value.ok_or_else(|| AdapterError::Evidence(format!("operation missing {field}")))
}

fn canonical_json(value: &Value) -> Result<Vec<u8>, AdapterError> {
    let mut bytes = serde_json::to_vec(value)?;
    bytes.push(b'\n');
    Ok(bytes)
}

async fn redis_case(
    case: Arc<Case>,
    concurrency: usize,
    namespace: &str,
) -> Result<(Value, Vec<String>, Vec<u64>, u64, u128, u64), AdapterError> {
    let url = env::var("ALPHA_REDIS_URL").unwrap_or_else(|_| DEFAULT_REDIS_URL.into());
    let config = Config::from_url(&url)?;
    let connection = ConnectionConfig {
        connection_timeout: Duration::from_millis(case.timeout_ms),
        internal_command_timeout: Duration::from_millis(case.timeout_ms),
        max_command_attempts: 1,
        max_command_buffer_len: case.pipeline_depth,
        ..Default::default()
    };
    let client = Pool::new(config, None, Some(connection), None, case.connection_count)?;
    let connection_task = client.init().await?;
    let suffix = &sha256_hex(namespace.as_bytes())[..8];
    let tick_key = format!("redis:cache:latest_ticks:M2{suffix}");
    let breaker_key = format!("hard_stop:lockout:mvp2-{suffix}");
    client
        .del::<(), _>(vec![tick_key.clone(), breaker_key.clone()])
        .await?;
    client.ping::<String>(None).await?;

    let errors = Arc::new(AtomicU64::new(0));
    let barrier = Arc::new(Barrier::new(concurrency));
    let semaphore = Arc::new(Semaphore::new(case.pipeline_depth));
    let mut tasks = Vec::with_capacity(concurrency);
    let counts = distribute(case.repeat, concurrency);
    let mut first = 0;
    for iterations in counts {
        let task_case = Arc::clone(&case);
        let task_client = client.clone();
        let task_errors = Arc::clone(&errors);
        let task_barrier = Arc::clone(&barrier);
        let task_semaphore = Arc::clone(&semaphore);
        let task_first = first;
        let task_tick_key = tick_key.clone();
        let task_breaker_key = breaker_key.clone();
        first += iterations;
        tasks.push(tokio::spawn(async move {
            let mut completed = Vec::with_capacity(iterations * task_case.operations.len());
            task_barrier.wait().await;
            for iteration in task_first..task_first + iterations {
                match redis_iteration(
                    &task_client,
                    &task_case.operations,
                    iteration,
                    &task_tick_key,
                    &task_breaker_key,
                    &task_semaphore,
                )
                .await
                {
                    Ok(mut values) => completed.append(&mut values),
                    Err(_) => {
                        task_errors.fetch_add(1, Ordering::Relaxed);
                    }
                }
            }
            completed
        }));
    }
    let cpu_before = linux_cpu_ns()?;
    let started = Instant::now();
    let mut completed = Vec::with_capacity(case.repeat * case.operations.len());
    for task in tasks {
        completed.append(&mut task.await?);
    }
    let elapsed = started.elapsed().as_nanos();
    let cpu_ns = linux_cpu_ns()?.saturating_sub(cpu_before);
    let stored: Option<String> = client.get(&tick_key).await?;
    let ttl: i64 = client.ttl(&tick_key).await?;
    let locked: i64 = client.exists(&breaker_key).await?;
    let terminal = json!([
        {"id": "r001", "stored": stored},
        {"id": "r002", "ttl_positive": ttl > 0},
        {"id": "r003", "locked": locked != 0}
    ]);
    client.quit().await?;
    connection_task.await??;
    completed.sort_by(|left, right| left.0.cmp(&right.0));
    let (tokens, latencies) = completed.into_iter().unzip();
    Ok((
        terminal,
        tokens,
        latencies,
        errors.load(Ordering::Relaxed),
        elapsed,
        cpu_ns,
    ))
}

async fn redis_iteration(
    client: &Pool,
    operations: &[Operation],
    iteration: usize,
    tick_key: &str,
    breaker_key: &str,
    semaphore: &Semaphore,
) -> Result<Vec<(String, u64)>, AdapterError> {
    let mut completed = Vec::with_capacity(operations.len());
    for operation in operations {
        let started = Instant::now();
        let _permit = semaphore
            .acquire()
            .await
            .map_err(|_| AdapterError::Evidence("Redis bounded queue closed".into()))?;
        match operation.op.as_str() {
            "set_latest_tick" => {
                client
                    .set::<(), _, _>(
                        tick_key,
                        required(&operation.value, "value")?,
                        Some(Expiration::EX(60)),
                        None,
                        false,
                    )
                    .await?;
            }
            "get_latest_tick" => {
                let actual: Option<String> = client.get(tick_key).await?;
                if actual.as_deref() != operation.expected.as_ref().and_then(Value::as_str) {
                    return Err(AdapterError::Evidence("Redis tick mismatch".into()));
                }
            }
            "breaker_check" => {
                let actual: i64 = client.exists(breaker_key).await?;
                if (actual != 0)
                    != operation
                        .expected
                        .as_ref()
                        .and_then(Value::as_bool)
                        .unwrap_or(true)
                {
                    return Err(AdapterError::Evidence("Redis breaker mismatch".into()));
                }
            }
            other => {
                return Err(AdapterError::Evidence(format!(
                    "unknown Redis operation {other}"
                )))
            }
        }
        completed.push((
            format!("{iteration:06}:{}", operation.id),
            started.elapsed().as_nanos().max(1) as u64,
        ));
    }
    Ok(completed)
}

async fn db_case(
    case: Arc<Case>,
    concurrency: usize,
    namespace: &str,
) -> Result<(Value, Vec<String>, Vec<u64>, u64, u128, u64), AdapterError> {
    let url = env::var("ALPHA_POSTGRES_URL").unwrap_or_else(|_| DEFAULT_POSTGRES_URL.into());
    let pool = PgPoolOptions::new()
        .min_connections(case.connection_count as u32)
        .max_connections(case.connection_count as u32)
        .acquire_timeout(Duration::from_millis(case.timeout_ms))
        .connect(&url)
        .await?;
    let ticker = format!("M2{}", &sha256_hex(namespace.as_bytes())[..8]);
    sqlx::query("DELETE FROM portfolio_positions WHERE ticker=$1")
        .bind(&ticker)
        .execute(&pool)
        .await?;
    sqlx::query_scalar::<_, i32>("SELECT 1")
        .fetch_one(&pool)
        .await?;

    let errors = Arc::new(AtomicU64::new(0));
    let barrier = Arc::new(Barrier::new(concurrency));
    let mut tasks = Vec::with_capacity(concurrency);
    let counts = distribute(case.repeat, concurrency);
    let mut first = 0;
    for iterations in counts {
        let task_case = Arc::clone(&case);
        let task_pool = pool.clone();
        let task_errors = Arc::clone(&errors);
        let task_barrier = Arc::clone(&barrier);
        let task_first = first;
        let task_ticker = ticker.clone();
        first += iterations;
        tasks.push(tokio::spawn(async move {
            let mut completed = Vec::with_capacity(iterations * task_case.operations.len());
            task_barrier.wait().await;
            for iteration in task_first..task_first + iterations {
                match db_iteration(&task_pool, &task_case.operations, iteration, &task_ticker).await
                {
                    Ok(mut values) => completed.append(&mut values),
                    Err(_) => {
                        task_errors.fetch_add(1, Ordering::Relaxed);
                    }
                }
            }
            completed
        }));
    }
    let cpu_before = linux_cpu_ns()?;
    let started = Instant::now();
    let mut completed = Vec::with_capacity(case.repeat * case.operations.len());
    for task in tasks {
        completed.append(&mut task.await?);
    }
    let elapsed = started.elapsed().as_nanos();
    let cpu_ns = linux_cpu_ns()?.saturating_sub(cpu_before);
    let rows: Vec<(String, i64, i64)> = sqlx::query_as(
        "SELECT strategy_id,quantity::bigint,current_price::bigint FROM portfolio_positions \
         WHERE ticker=$1 ORDER BY strategy_id",
    )
    .bind(&ticker)
    .fetch_all(&pool)
    .await?;
    if rows.len() != 2 {
        return Err(AdapterError::Evidence(
            "PostgreSQL terminal row count differs".into(),
        ));
    }
    let exposure: (i64, i64) = sqlx::query_as(
        "SELECT COALESCE(SUM(quantity),0)::bigint, \
         COALESCE(SUM(quantity*current_price),0)::bigint \
         FROM portfolio_positions WHERE ticker=$1 AND quantity>0",
    )
    .bind(&ticker)
    .fetch_one(&pool)
    .await?;
    let terminal = json!([
        {"id": "d001", "strategy_id": rows[0].0, "quantity": rows[0].1, "current_price": rows[0].2},
        {"id": "d002", "strategy_id": rows[1].0, "quantity": rows[1].1, "current_price": rows[1].2},
        {"id": "d003", "total_quantity": exposure.0, "total_market_value": exposure.1}
    ]);
    sqlx::query("DELETE FROM portfolio_positions WHERE ticker=$1")
        .bind(&ticker)
        .execute(&pool)
        .await?;
    pool.close().await;
    completed.sort_by(|left, right| left.0.cmp(&right.0));
    let (tokens, latencies) = completed.into_iter().unzip();
    Ok((
        terminal,
        tokens,
        latencies,
        errors.load(Ordering::Relaxed),
        elapsed,
        cpu_ns,
    ))
}

async fn db_iteration(
    pool: &sqlx::PgPool,
    operations: &[Operation],
    iteration: usize,
    ticker: &str,
) -> Result<Vec<(String, u64)>, AdapterError> {
    let mut completed = Vec::with_capacity(operations.len());
    for operation in operations {
        let started = Instant::now();
        match operation.op.as_str() {
            "upsert_position" => {
                sqlx::query(
                    "INSERT INTO portfolio_positions \
                     (ticker,name,quantity,avg_price,current_price,is_paper,account_scope,strategy_id,opened_at,updated_at) \
                     VALUES ($1,$2,$3,1000,$4,true,$5,$6,NOW(),NOW()) \
                     ON CONFLICT (ticker,account_scope,COALESCE(strategy_id,'')) DO UPDATE SET \
                     quantity=EXCLUDED.quantity,current_price=EXCLUDED.current_price,updated_at=NOW()",
                )
                .bind(ticker)
                .bind("MVP-2 fixture")
                .bind(required_i64(operation.quantity, "quantity")?)
                .bind(required_i64(operation.current_price, "current_price")?)
                .bind(required(&operation.account_scope, "account_scope")?)
                .bind(required(&operation.strategy_id, "strategy_id")?)
                .execute(pool)
                .await?;
            }
            "read_exposure" => {
                let actual: (i64, i64) = sqlx::query_as(
                    "SELECT COALESCE(SUM(quantity),0)::bigint, \
                     COALESCE(SUM(quantity*current_price),0)::bigint \
                     FROM portfolio_positions WHERE ticker=$1 AND quantity>0",
                )
                .bind(ticker)
                .fetch_one(pool)
                .await?;
                let expected = operation
                    .expected
                    .as_ref()
                    .ok_or_else(|| AdapterError::Evidence("operation missing expected".into()))?;
                if actual.0 != expected["total_quantity"].as_i64().unwrap_or(i64::MIN)
                    || actual.1 != expected["total_market_value"].as_i64().unwrap_or(i64::MIN)
                {
                    return Err(AdapterError::Evidence(
                        "PostgreSQL exposure mismatch".into(),
                    ));
                }
            }
            other => {
                return Err(AdapterError::Evidence(format!(
                    "unknown PostgreSQL operation {other}"
                )))
            }
        }
        completed.push((
            format!("{iteration:06}:{}", operation.id),
            started.elapsed().as_nanos().max(1) as u64,
        ));
    }
    Ok(completed)
}

fn linux_cpu_ns() -> Result<u64, AdapterError> {
    let value = fs::read_to_string("/proc/self/schedstat")?;
    value
        .split_whitespace()
        .next()
        .and_then(|field| field.parse().ok())
        .ok_or_else(|| AdapterError::Evidence("invalid /proc/self/schedstat".into()))
}

fn linux_peak_rss_bytes() -> Result<u64, AdapterError> {
    let status = fs::read_to_string("/proc/self/status")?;
    let kib = status
        .lines()
        .find_map(|line| line.strip_prefix("VmHWM:"))
        .and_then(|line| line.split_whitespace().next())
        .and_then(|field| field.parse::<u64>().ok())
        .ok_or_else(|| AdapterError::Evidence("VmHWM unavailable in /proc/self/status".into()))?;
    Ok(kib * 1024)
}

async fn run(args: Args) -> Result<Output, AdapterError> {
    if !cfg!(target_os = "linux") {
        return Err(AdapterError::Evidence(
            "real CPU/RSS measurement requires Linux /proc".into(),
        ));
    }
    let fixture_bytes = fs::read(&args.fixture)?;
    let fixture: Fixture = serde_json::from_slice(&fixture_bytes)?;
    let case = if args.case == "redis-hot-path" {
        fixture.cases.redis_hot_path
    } else {
        fixture.cases.db_read_write
    };
    if case.connection_count != 1
        || case.pipeline_depth != fixture.contract.pipeline_depth
        || fixture.contract.pipeline_depth != 1
        || fixture.contract.queue_depth != 1
        || fixture.contract.retry_policy != "none"
        || fixture.contract.saturation_policy != "bounded_wait"
    {
        return Err(AdapterError::Evidence(
            "fixture driver bounds differ".into(),
        ));
    }
    let operation_count = case.operations.len() * case.repeat;
    let case = Arc::new(case);
    let (terminal, completed_tokens, operation_latency_ns, errors, elapsed_ns, cpu_ns) =
        if args.case == "redis-hot-path" {
            redis_case(Arc::clone(&case), args.concurrency, &args.namespace).await?
        } else {
            db_case(Arc::clone(&case), args.concurrency, &args.namespace).await?
        };
    let result_sha256 = sha256_hex(&canonical_json(&terminal)?);
    if terminal != case.terminal || result_sha256 != case.terminal_sha256 {
        return Err(AdapterError::Evidence(
            "terminal state differs from committed golden".into(),
        ));
    }
    let _trial = args.trial;
    Ok(Output {
        elapsed_ms: elapsed_ns as f64 / 1_000_000.0,
        fixture_sha256: sha256_hex(&fixture_bytes),
        completed_tokens,
        operation_latency_ns,
        terminal,
        result_sha256,
        errors,
        dropped: 0,
        configuration: Configuration {
            concurrency: args.concurrency,
            operation_count,
            pipeline_depth: case.pipeline_depth,
            timeout_ms: case.timeout_ms,
            event_loop_mode: "tokio",
            worker_count: 1,
            queue_depth: case.pipeline_depth,
            connection_count: case.connection_count,
            retry_policy: fixture.contract.retry_policy,
            saturation_policy: fixture.contract.saturation_policy,
            service_config_sha256: fixture.contract.service_config_sha256,
            schema_sha256: fixture.contract.schema_sha256,
        },
        resources: Resources {
            peak_rss_bytes: linux_peak_rss_bytes()?,
            cpu_time_ms: cpu_ns as f64 / 1_000_000.0,
        },
        build: Build {
            runtime: "tokio 1.43.0 / sqlx 0.8.6 / fred 10.1.0",
            compiler: "rustc workspace rust-version 1.91.1",
            flags: "cargo --release; bounded queue; zero driver retries",
            dependencies: if args.case == "redis-hot-path" {
                json!({"fred": "10.1.0"})
            } else {
                json!({"sqlx": "0.8.6"})
            },
        },
    })
}

fn sha256_hex(input: &[u8]) -> String {
    let mut state: [u32; 8] = [
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab,
        0x5be0cd19,
    ];
    let bit_len = (input.len() as u64) * 8;
    let mut padded = input.to_vec();
    padded.push(0x80);
    while padded.len() % 64 != 56 {
        padded.push(0);
    }
    padded.extend_from_slice(&bit_len.to_be_bytes());
    for chunk in padded.chunks_exact(64) {
        sha256_compress(&mut state, chunk);
    }
    state.iter().map(|word| format!("{word:08x}")).collect()
}

fn sha256_compress(state: &mut [u32; 8], chunk: &[u8]) {
    const K: [u32; 64] = [
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2,
    ];
    let mut schedule = [0_u32; 64];
    for (index, word) in chunk.chunks_exact(4).enumerate() {
        schedule[index] = u32::from_be_bytes([word[0], word[1], word[2], word[3]]);
    }
    for index in 16..64 {
        let s0 = schedule[index - 15].rotate_right(7)
            ^ schedule[index - 15].rotate_right(18)
            ^ (schedule[index - 15] >> 3);
        let s1 = schedule[index - 2].rotate_right(17)
            ^ schedule[index - 2].rotate_right(19)
            ^ (schedule[index - 2] >> 10);
        schedule[index] = schedule[index - 16]
            .wrapping_add(s0)
            .wrapping_add(schedule[index - 7])
            .wrapping_add(s1);
    }
    let [mut a, mut b, mut c, mut d, mut e, mut f, mut g, mut h] = *state;
    for index in 0..64 {
        let sum1 = e.rotate_right(6) ^ e.rotate_right(11) ^ e.rotate_right(25);
        let choice = (e & f) ^ ((!e) & g);
        let temp1 = h
            .wrapping_add(sum1)
            .wrapping_add(choice)
            .wrapping_add(K[index])
            .wrapping_add(schedule[index]);
        let sum0 = a.rotate_right(2) ^ a.rotate_right(13) ^ a.rotate_right(22);
        let majority = (a & b) ^ (a & c) ^ (b & c);
        let temp2 = sum0.wrapping_add(majority);
        h = g;
        g = f;
        f = e;
        e = d.wrapping_add(temp1);
        d = c;
        c = b;
        b = a;
        a = temp1.wrapping_add(temp2);
    }
    for (slot, value) in state.iter_mut().zip([a, b, c, d, e, f, g, h]) {
        *slot = slot.wrapping_add(value);
    }
}

#[tokio::main(flavor = "multi_thread", worker_threads = 1)]
async fn main() {
    let result = match parse_args(env::args()) {
        Ok(args) => run(args).await,
        Err(error) => Err(error),
    };
    match result.and_then(|output| serde_json::to_string(&output).map_err(AdapterError::from)) {
        Ok(output) => println!("{output}"),
        Err(error) => {
            eprintln!("{error}");
            std::process::exit(1);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::{canonical_json, distribute, parse_args, sha256_hex};
    use serde_json::json;

    #[test]
    fn sha256_matches_standard_vectors() {
        assert_eq!(
            sha256_hex(b""),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
        );
        assert_eq!(
            sha256_hex(b"abc"),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
        );
    }

    #[test]
    fn terminal_json_matches_python_canonical_checksum() {
        let redis = json!([
            {"id": "r001", "stored": "{\"price\":70000,\"volume\":10}"},
            {"id": "r002", "ttl_positive": true},
            {"id": "r003", "locked": false}
        ]);
        let bytes = canonical_json(&redis).expect("static JSON must serialize");
        assert_eq!(
            sha256_hex(&bytes),
            "e2c7ca827491db79926ca9e8c9fcb71ee5ceba381e1d71db487b188871d9f469"
        );
    }

    #[test]
    fn work_distribution_is_complete_and_balanced() {
        let values = distribute(1000, 32);
        assert_eq!(values.iter().sum::<usize>(), 1000);
        assert_eq!(
            values.iter().max().unwrap_or(&0) - values.iter().min().unwrap_or(&0),
            1
        );
    }

    #[test]
    fn arguments_fail_closed() {
        let values = [
            "adapter",
            "--fixture",
            "fixture.json",
            "--case",
            "bad",
            "--concurrency",
            "8",
            "--trial",
            "0",
        ];
        assert!(parse_args(values.into_iter().map(String::from)).is_err());
    }
}
