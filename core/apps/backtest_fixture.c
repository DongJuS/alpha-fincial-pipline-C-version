#include "backtest_fixture.h"

#include <stdlib.h>
#include <string.h>

#include "alpha/date.h"
#include "yyjson.h"

static alpha_err_t parse_signal(const char *text, alpha_signal_t *out) {
    if (strcmp(text, "BUY") == 0) {
        *out = ALPHA_SIGNAL_BUY;
    } else if (strcmp(text, "SELL") == 0) {
        *out = ALPHA_SIGNAL_SELL;
    } else if (strcmp(text, "HOLD") == 0) {
        *out = ALPHA_SIGNAL_HOLD;
    } else if (strcmp(text, "CLOSE") == 0) {
        *out = ALPHA_SIGNAL_CLOSE;
    } else {
        return ALPHA_ERR_INVALID_ARG;
    }
    return ALPHA_OK;
}

static alpha_err_t parse_config(yyjson_val *cfg, alpha_backtest_config_t *out) {
    if (cfg == NULL || !yyjson_is_obj(cfg)) {
        return ALPHA_ERR_INVALID_ARG;
    }
    const char *ticker = yyjson_get_str(yyjson_obj_get(cfg, "ticker"));
    const char *strategy = yyjson_get_str(yyjson_obj_get(cfg, "strategy"));
    if (ticker == NULL || strategy == NULL) {
        return ALPHA_ERR_INVALID_ARG;
    }
    strncpy(out->ticker, ticker, ALPHA_TICKER_CAP - 1);
    out->ticker[ALPHA_TICKER_CAP - 1] = '\0';
    strncpy(out->strategy, strategy, ALPHA_STRATEGY_CAP - 1);
    out->strategy[ALPHA_STRATEGY_CAP - 1] = '\0';

    struct {
        const char *key;
        int64_t *dst;
    } dates[] = {
        {"train_start", &out->train_start},
        {"train_end", &out->train_end},
        {"test_start", &out->test_start},
        {"test_end", &out->test_end},
    };
    for (size_t i = 0; i < sizeof(dates) / sizeof(dates[0]); ++i) {
        const char *iso = yyjson_get_str(yyjson_obj_get(cfg, dates[i].key));
        if (iso == NULL || alpha_date_parse(iso, dates[i].dst) != ALPHA_OK) {
            return ALPHA_ERR_INVALID_ARG;
        }
    }

    out->initial_capital = yyjson_get_sint(yyjson_obj_get(cfg, "initial_capital"));
    out->commission_rate_pct = yyjson_get_num(yyjson_obj_get(cfg, "commission_rate_pct"));
    out->tax_rate_pct = yyjson_get_num(yyjson_obj_get(cfg, "tax_rate_pct"));
    out->slippage_bps = (int)yyjson_get_sint(yyjson_obj_get(cfg, "slippage_bps"));
    return ALPHA_OK;
}

static alpha_err_t parse_bars(yyjson_val *bars, alpha_fixture_t *out) {
    if (!yyjson_is_arr(bars)) {
        return ALPHA_ERR_INVALID_ARG;
    }
    out->bar_count = yyjson_arr_size(bars);
    out->prices = (double *)malloc(out->bar_count * sizeof(double));
    out->dates = (int64_t *)malloc(out->bar_count * sizeof(int64_t));
    if (out->prices == NULL || out->dates == NULL) {
        return ALPHA_ERR_IO;
    }
    size_t idx = 0;
    yyjson_val *bar = NULL;
    yyjson_arr_iter iter = yyjson_arr_iter_with(bars);
    while ((bar = yyjson_arr_iter_next(&iter)) != NULL) {
        const char *iso = yyjson_get_str(yyjson_obj_get(bar, "date"));
        if (iso == NULL || alpha_date_parse(iso, &out->dates[idx]) != ALPHA_OK) {
            return ALPHA_ERR_INVALID_ARG;
        }
        out->prices[idx] = yyjson_get_num(yyjson_obj_get(bar, "close"));
        idx += 1;
    }
    return ALPHA_OK;
}

static alpha_err_t parse_signals(yyjson_val *signals, alpha_fixture_t *out) {
    if (signals == NULL || !yyjson_is_obj(signals)) {
        return ALPHA_OK; /* signals are optional */
    }
    out->signal_count = yyjson_obj_size(signals);
    out->signal_days = (int64_t *)malloc(out->signal_count * sizeof(int64_t));
    out->signal_values = (alpha_signal_t *)malloc(out->signal_count * sizeof(alpha_signal_t));
    if (out->signal_days == NULL || out->signal_values == NULL) {
        return ALPHA_ERR_IO;
    }
    size_t idx = 0;
    yyjson_val *key = NULL;
    yyjson_obj_iter iter = yyjson_obj_iter_with(signals);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL) {
        const char *iso = yyjson_get_str(key);
        const char *sig = yyjson_get_str(yyjson_obj_iter_get_val(key));
        if (iso == NULL || sig == NULL ||
            alpha_date_parse(iso, &out->signal_days[idx]) != ALPHA_OK ||
            parse_signal(sig, &out->signal_values[idx]) != ALPHA_OK) {
            return ALPHA_ERR_INVALID_ARG;
        }
        idx += 1;
    }
    return ALPHA_OK;
}

alpha_err_t alpha_fixture_load(const char *path, alpha_fixture_t *out) {
    if (path == NULL || out == NULL) {
        return ALPHA_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    yyjson_doc *doc = yyjson_read_file(path, 0, NULL, NULL);
    if (doc == NULL) {
        return ALPHA_ERR_IO;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    alpha_err_t status = ALPHA_ERR_INVALID_ARG;
    if (yyjson_is_obj(root)) {
        status = parse_config(yyjson_obj_get(root, "config"), &out->config);
        if (status == ALPHA_OK) {
            status = parse_bars(yyjson_obj_get(root, "bars"), out);
        }
        if (status == ALPHA_OK) {
            status = parse_signals(yyjson_obj_get(root, "signals"), out);
        }
    }

    yyjson_doc_free(doc);
    if (status != ALPHA_OK) {
        alpha_fixture_free(out);
    }
    return status;
}

void alpha_fixture_free(alpha_fixture_t *fixture) {
    if (fixture == NULL) {
        return;
    }
    free(fixture->prices);
    free(fixture->dates);
    free(fixture->signal_days);
    free(fixture->signal_values);
    memset(fixture, 0, sizeof(*fixture));
}

alpha_replay_source_t alpha_fixture_replay(const alpha_fixture_t *fixture) {
    alpha_replay_source_t source;
    source.days = fixture->signal_days;
    source.signals = fixture->signal_values;
    source.count = fixture->signal_count;
    return source;
}
