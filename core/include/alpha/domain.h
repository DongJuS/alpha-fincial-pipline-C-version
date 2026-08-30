#ifndef ALPHA_DOMAIN_H
#define ALPHA_DOMAIN_H

typedef enum {
    ALPHA_SIGNAL_BUY,
    ALPHA_SIGNAL_SELL,
    ALPHA_SIGNAL_HOLD,
    ALPHA_SIGNAL_CLOSE,
} alpha_signal_t;

typedef enum { ALPHA_SIDE_BUY, ALPHA_SIDE_SELL } alpha_side_t;

typedef enum {
    ALPHA_MARKET_KOSPI,
    ALPHA_MARKET_KOSDAQ,
    ALPHA_MARKET_NYSE,
    ALPHA_MARKET_NASDAQ,
} alpha_market_t;

#endif
