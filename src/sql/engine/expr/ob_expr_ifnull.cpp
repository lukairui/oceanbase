/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/expr/ob_expr_ifnull.h"

#include "share/object/ob_obj_cast.h"

#include "sql/engine/expr/ob_expr_promotion_util.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase {
using namespace oceanbase::common;
namespace sql {

ObExprIfNull::ObExprIfNull(ObIAllocator& alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_IFNULL, N_IFNULL, 2, NOT_ROW_DIMENSION)
{}

ObExprIfNull::~ObExprIfNull()
{}

int ObExprIfNull::calc_result_type2(
    ObExprResType& type, ObExprResType& type1, ObExprResType& type2, ObExprTypeCtx& type_ctx) const
{
  int ret = OB_SUCCESS;
  const ObSQLSessionInfo* session = type_ctx.get_session();
  if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is NULL", K(ret));
  } else if (OB_FAIL(ObExprPromotionUtil::get_nvl_type(type, type1, type2))) {
    LOG_WARN("failed to get nvl type", K(ret));
  } else if (ob_is_string_type(type.get_type())) {
    ObCollationLevel res_cs_level = CS_LEVEL_INVALID;
    ObCollationType res_cs_type = CS_TYPE_INVALID;
    if (OB_FAIL(ObCharset::aggregate_collation(type1.get_collation_level(),
            type1.get_collation_type(),
            type2.get_collation_level(),
            type2.get_collation_type(),
            res_cs_level,
            res_cs_type))) {
      LOG_WARN("failed to calc collation", K(ret));
    } else {
      type.set_collation_level(res_cs_level);
      type.set_collation_type(res_cs_type);
    }
  }

  if (OB_SUCC(ret)) {
    if (type.get_type() == type1.get_type()) {
      type.set_accuracy(type1.get_accuracy());
    } else {
      type.set_accuracy(type2.get_accuracy());
    }
    // For the mixed type of int and uint64, the type needs to be promoted to decimal
    if ((ObUInt64Type == type1.get_type() || ObUInt64Type == type2.get_type()) && ObIntType == type.get_type()) {
      type.set_type(ObNumberType);
      type.set_accuracy(ObAccuracy::DDL_DEFAULT_ACCURACY[ObIntType].get_accuracy());
    }

    //set scale
    ObScale scale1 = type1.is_null() ? 0 : type1.get_scale();
    ObScale scale2 = type2.is_null() ? 0 : type2.get_scale();
    if (-1 != scale1 && -1 != scale2) {
      type.set_scale(static_cast<ObScale>(max(scale1, scale2)));
    } else {
      type.set_scale(-1);
    }
    type.set_length(MAX(type1.get_length(), type2.get_length()));
    if (session->use_static_typing_engine()) {
      type1.set_calc_meta(type.get_obj_meta());
      type1.set_calc_accuracy(type.get_accuracy());
      type2.set_calc_meta(type.get_obj_meta());
      type2.set_calc_accuracy(type.get_accuracy());
    } else {
      if (ob_is_enumset_tc(type1.get_type())) {
        type1.set_calc_type(type.get_type());
      }
      if (ob_is_enumset_tc(type2.get_type())) {
        type2.set_calc_type(type.get_type());
      }
    }
  }

  return ret;
}

int ObExprIfNull::calc_result2(ObObj& result, const ObObj& obj1, const ObObj& obj2, ObExprCtx& expr_ctx) const
{
  int ret = OB_SUCCESS;
  const ObObj* p_result = NULL;
  ObObjType res_type = get_result_type().get_type();
  const ObObj* p_target = obj1.is_null() ? &obj2 : &obj1;
  ObObj casted_obj;
  EXPR_DEFINE_CAST_CTX(expr_ctx, CM_NONE);
  if (OB_FAIL(ObObjCaster::to_type(res_type, cast_ctx, *p_target, casted_obj, p_result))) {
    LOG_WARN("failed to cast object", K(ret), K(*p_target), K(res_type));
  } else if (NULL != p_result) {
    result = *p_result;
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to cast object", K(ret), K(*p_target), K(res_type));
  }
  return ret;
}

int ObExprIfNull::calc_ifnull_expr(const ObExpr& expr, ObEvalCtx& ctx, ObDatum& res_datum)
{
  int ret = OB_SUCCESS;
  // ifnull(arg0, arg1);
  ObDatum* arg0 = NULL;
  ObDatum* arg1 = NULL;
  if (OB_FAIL(expr.args_[0]->eval(ctx, arg0))) {
    LOG_WARN("eval arg0 failed", K(ret));
  } else if (!arg0->is_null()) {
    res_datum.set_datum(*arg0);
  } else if (OB_FAIL(expr.args_[1]->eval(ctx, arg1))) {
    LOG_WARN("eval arg1 failed", K(ret));
  } else {
    res_datum.set_datum(*arg1);
  }
  return ret;
}

int ObExprIfNull::cg_expr(ObExprCGCtx& expr_cg_ctx, const ObRawExpr& raw_expr, ObExpr& rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = calc_ifnull_expr;
  return ret;
}
}  // namespace sql
}  // namespace oceanbase
