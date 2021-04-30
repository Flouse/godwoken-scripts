/*
 * SUDT Utils
 * Godwoken backend use this utils to modify SUDT states from the SMT.
 */

#include "godwoken.h"
#include "gw_def.h"
#include "stdio.h"

/* errors */
#define ERROR_INSUFFICIENT_BALANCE 12
#define ERROR_AMOUNT_OVERFLOW 13
#define ERROR_TO_ID 14
#define ERROR_ACCOUNT_NOT_EXISTS 15

/* Prepare withdrawal fields */
#define WITHDRAWAL_LOCK_HASH 1
#define WITHDRAWAL_AMOUNT 2
#define WITHDRAWAL_BLOCK_NUMBER 3

void _sudt_id_to_key(const uint32_t account_id, uint8_t key[32]) {
  memcpy(key, (uint8_t *)&account_id, 4);
}

int _account_exists(gw_context_t *ctx, uint32_t account_id, bool *exists) {
  uint8_t script_hash[32];
  int ret = ctx->sys_get_script_hash_by_account_id(ctx, account_id, script_hash);
  if (ret != 0) {
    return ret;
  }
  *exists = false;
  for (int i = 0; i < 32; i++) {
    /* if account not exists script_hash will be zero */
    if (script_hash[i] != 0) {
      *exists = true;
      break;
    }
  }
  return 0;
}

int _sudt_emit_log(gw_context_t *ctx, uint32_t sudt_id,
                   uint32_t from_id, uint32_t to_id,
                   uint128_t amount,
                   uint8_t service_flag) {
#ifdef GW_VALIDATOR
  uint32_t data_size = 0;
  uint8_t *data = NULL;
#else
  static const uint32_t data_size = 4 + 4 + 16;
  uint8_t data[4 + 4 + 16] = {0};
  memcpy(data, (uint8_t *)(&from_id), 4);
  memcpy(data + 4, (uint8_t *)(&to_id), 4);
  memcpy(data + 4 + 4, (uint8_t *)(&amount), 16);
#endif
  return ctx->sys_log(ctx, sudt_id, service_flag, data_size, data);
}

int _sudt_get_balance(gw_context_t *ctx, uint32_t sudt_id,
                      const uint8_t key[32], uint128_t *balance) {
  uint8_t value[32] = {0};
  int ret = ctx->sys_load(ctx, sudt_id, key, value);
  if (ret != 0) {
    return ret;
  }
  *balance = *(uint128_t *)value;
  return 0;
}

int _sudt_set_balance(gw_context_t *ctx, uint32_t sudt_id, uint8_t key[32],
                      uint128_t balance) {
  uint8_t value[32] = {0};
  *(uint128_t *)value = balance;
  int ret = ctx->sys_store(ctx, sudt_id, key, value);
  return ret;
}

int sudt_get_balance(gw_context_t *ctx, uint32_t sudt_id, uint32_t account_id,
                     uint128_t *balance) {
  int ret = gw_verify_sudt_account(ctx, sudt_id);
  if (ret != 0) {
    return ret;
  }
  bool exists = false;
  ret = _account_exists(ctx, account_id, &exists);
  if (ret != 0 || !exists) {
    return ERROR_ACCOUNT_NOT_EXISTS;
  }
  uint8_t key[32] = {0};
  _sudt_id_to_key(account_id, key);
  return _sudt_get_balance(ctx, sudt_id, key, balance);
}

/* Transfer Simple UDT */
int _sudt_transfer(gw_context_t *ctx, uint32_t sudt_id, uint32_t from_id,
                   uint32_t to_id, uint128_t amount,
                   uint8_t service_flag) {
  int ret;
  if (from_id == to_id) {
    return ERROR_TO_ID;
  }
  ret = gw_verify_sudt_account(ctx, sudt_id);
  if (ret != 0) {
    return ret;
  }

  bool exists = false;
  ret = _account_exists(ctx, from_id, &exists);
  if (ret != 0 || !exists) {
    return ERROR_ACCOUNT_NOT_EXISTS;
  }
  ret = _account_exists(ctx, to_id, &exists);
  if (ret != 0 || !exists) {
    return ERROR_ACCOUNT_NOT_EXISTS;
  }

  /* check from account */
  uint8_t from_key[32] = {0};
  _sudt_id_to_key(from_id, from_key);
  uint128_t from_balance;
  ret = _sudt_get_balance(ctx, sudt_id, from_key, &from_balance);
  if (ret != 0) {
    return ret;
  }
  if (from_balance < amount) {
    return ERROR_INSUFFICIENT_BALANCE;
  }
  uint128_t new_from_balance = from_balance - amount;

  /* check to account */
  uint8_t to_key[32] = {0};
  _sudt_id_to_key(to_id, to_key);
  uint128_t to_balance;
  ret = _sudt_get_balance(ctx, sudt_id, to_key, &to_balance);
  if (ret != 0) {
    return ret;
  }
  uint128_t new_to_balance = to_balance + amount;
  if (new_to_balance < to_balance) {
    return ERROR_AMOUNT_OVERFLOW;
  }

  /* update balance */
  ret = _sudt_set_balance(ctx, sudt_id, from_key, new_from_balance);
  if (ret != 0) {
    return ret;
  }
  ret = _sudt_set_balance(ctx, sudt_id, to_key, new_to_balance);
  if (ret != 0) {
    return ret;
  }
  return _sudt_emit_log(ctx, sudt_id, from_id, to_id, amount, service_flag);
}

int sudt_transfer(gw_context_t *ctx, uint32_t sudt_id, uint32_t from_id,
                  uint32_t to_id, uint128_t amount) {
  return _sudt_transfer(ctx, sudt_id, from_id, to_id, amount, GW_LOG_SUDT_TRANSFER);
}

/* Pay fee */
int sudt_pay_fee(gw_context_t *ctx, uint32_t sudt_id, uint32_t from_id, uint128_t amount) {
  uint32_t to_id = ctx->block_info.block_producer_id;
  return _sudt_transfer(ctx, sudt_id, from_id, to_id, amount, GW_LOG_SUDT_PAY_FEE);
}
