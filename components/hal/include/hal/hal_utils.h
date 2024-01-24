/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Integer division operation
 *
 */
typedef enum {
    HAL_DIV_ROUND_DOWN,     /*!< Round the division down to the floor integer */
    HAL_DIV_ROUND_UP,       /*!< Round the division up to the ceiling integer */
    HAL_DIV_ROUND,          /*!< Round the division to the nearest integer (round up if fraction >= 1/2, round down if fraction < 1/2) */
} hal_utils_div_round_opt_t;

/**
 * @brief Clock infomation
 *
 */
typedef struct {
    uint32_t src_freq_hz;   /*!< Source clock frequency, unit: Hz */
    uint32_t exp_freq_hz;   /*!< Expected output clock frequency, unit: Hz */
    uint32_t max_integ;     /*!< The max value of the integral part */
    uint32_t min_integ;     /*!< The min value of the integral part, integer range: [min_integ, max_integ) */
    union {
        uint32_t max_fract;     /*!< The max value of the denominator and numerator, numerator range: [0, max_fract), denominator range: [1, max_fract)
                                 *   Please make sure max_fract > 2 when calculate the division with fractal part */
        hal_utils_div_round_opt_t round_opt;     /*!< Integer division operation. For the case that doesn't have fractal part, set this field to the to specify the rounding method  */
    };
} hal_utils_clk_info_t;

/**
 * @brief Members of clock division
 *
 */
typedef struct {
    uint32_t integer;       /*!< Integer part of division */
    uint32_t denominator;   /*!< Denominator part of division */
    uint32_t numerator;     /*!< Numerator part of division */
} hal_utils_clk_div_t;

/**
 * @brief Calculate the clock division with fractal part fast
 * @note  Speed first algorithm, Time complexity O(log n).
 *        About 8~10 times faster than the accurate algorithm
 *
 * @param[in]  clk_info     The clock infomation
 * @param[out] clk_div      The clock division with integral and fractal part
 * @return
 *      - 0: Failed to get the result because the division is out of range
 *      - others: The real output clock frequency
 */
uint32_t hal_utils_calc_clk_div_frac_fast(const hal_utils_clk_info_t *clk_info, hal_utils_clk_div_t *clk_div);

/**
 * @brief Calculate the clock division with fractal part accurately
 * @note  Accuracy first algorithm, Time complexity O(n).
 *        About 1~hundreds times more accurate than the fast algorithm
 *
 * @param[in]  clk_info     The clock infomation
 * @param[out] clk_div      The clock division with integral and fractal part
 * @return
 *      - 0: Failed to get the result because the division is out of range
 *      - others: The real output clock frequency
 */
uint32_t hal_utils_calc_clk_div_frac_accurate(const hal_utils_clk_info_t *clk_info, hal_utils_clk_div_t *clk_div);

/**
 * @brief Calculate the clock division without fractal part
 *
 * @param[in]  clk_info     The clock infomation
 * @param[out] int_div      The clock integral division
 * @return
 *      - 0: Failed to get the result because the division is out of range,
 *           but parameter `int_div` will still be assigned to min/max division that given in `clk_info`,
 *           incase the caller still want to use the min/max division in this case.
 *      - others: The real output clock frequency
 */
uint32_t hal_utils_calc_clk_div_integer(const hal_utils_clk_info_t *clk_info, uint32_t *int_div);

/**
 * @brief Reverse the bit order of an 8-bit unsigned integer
 *
 * @param n The 8-bit unsigned integer to be reversed
 * @return The 8-bit unsigned integer after reversing
 */
__attribute__((always_inline))
static inline uint8_t hal_utils_bitwise_reverse8(uint8_t n)
{
    n = ((n & 0xf0) >> 4) | ((n & 0x0f) << 4);
    n = ((n & 0xcc) >> 2) | ((n & 0x33) << 2);
    n = ((n & 0xaa) >> 1) | ((n & 0x55) << 1);
    return n;
}

/**
 * @brief helper function, calculate the Greatest Common Divisor
 * @note gcd(a, b) = gcd(b, a % b)
 * @param a bigger value
 * @param b smaller value
 * @return result of gcd(a, b)
 */
__attribute__((always_inline))
static inline uint32_t _gcd(uint32_t a, uint32_t b)
{
    uint32_t c = a % b;
    while (c != 0) {
        a = b;
        b = c;
        c = a % b;
    }
    return b;
}

/**
 * @brief Get the least common multiple of two integer
 *
 * @param[in]  Integer A
 * @param[in]  Integer B
 *
 * @return     LCM of A and B
 */
__attribute__((always_inline))
static inline uint32_t _lcm(uint32_t a, uint32_t b)
{
    a = a == 0 ? 1 : a;
    b = b == 0 ? 1 : b;
    return (a * b / _gcd(a, b));
}

/**
 * @brief Get the least common multiple of three integer
 *
 * @param[in]  Integer A
 * @param[in]  Integer B
 * @param[in]  Integer C
 *
 * @return     LCM of A, B and C
 */
__attribute__((always_inline))
static inline uint32_t _lcm_3(uint32_t a, uint32_t b, uint32_t c)
{
    a = a == 0 ? 1 : a;
    b = b == 0 ? 1 : b;
    c = c == 0 ? 1 : c;
    return _lcm(a, _lcm(b, c));
}

#ifdef __cplusplus
}
#endif
