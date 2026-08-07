/**
 * @name ZIL replay flexible array left un-byteswapped
 * @description Detects byteswap_uint*_array(p, sizeof(*p)) / sizeof(T) on a
 *              log-record type that ends in a multi-byte flexible array member
 *              (e.g. blkptr_t lr_bps[]). sizeof() only covers the fixed header,
 *              so the trailing FAM stays foreign-endian on opposite-endian ZIL
 *              replay. Opaque uint8_t[] FAMs (names, raw write data) are ignored.
 *              See TX_CLONE_RANGE replay (zfs_replay_clone_range /
 *              zvol_replay_clone_range).
 * @kind problem
 * @severity error
 * @tags correctness
 *       endianness
 * @id cpp/zilFamByteswap
 */

import cpp

/**
 * A struct that ends with a C99 flexible array member or a GCC zero-length
 * array used the same way.
 */
class FlexibleArrayStruct extends Struct {
  Field fam;

  FlexibleArrayStruct() {
    fam = this.getAField() and
    (
      (
        fam.getType() instanceof ArrayType and
        not fam.getType().(ArrayType).hasArraySize()
      )
      or
      (
        fam.getType() instanceof ArrayType and
        fam.getType().(ArrayType).hasArraySize() and
        fam.getType().(ArrayType).getArraySize() = 0
      )
    ) and
    not exists(Field f2 |
      f2 = this.getAField() and f2.getByteOffset() > fam.getByteOffset()
    )
  }

  Field getFam() { result = fam }

  Type getFamElementType() {
    result = fam.getType().(ArrayType).getBaseType().getUnspecifiedType()
  }

  /** Prefer typedef name (lr_clone_range_t) over the tag name. */
  string getDisplayName() {
    exists(TypedefType t |
      t.getBaseType().getUnspecifiedType() = this and result = t.getName()
    )
    or
    (
      not exists(TypedefType t | t.getBaseType().getUnspecifiedType() = this) and
      this.getName() != "" and
      result = this.getName()
    )
    or
    (
      not exists(TypedefType t | t.getBaseType().getUnspecifiedType() = this) and
      this.getName() = "" and
      result = "<anonymous struct>"
    )
  }

  /**
   * FAM holds multi-byte structured data that a header-only sizeof() byteswap
   * would leave wrong-endian.
   */
  predicate hasStructuredFam() {
    exists(Type et | et = this.getFamElementType() | et.getSize() > 1)
  }
}

class ByteswapCall extends FunctionCall {
  ByteswapCall() {
    this.getTarget().getName().regexpMatch("byteswap_uint(64|32|16)_array")
  }

  Expr getBufferArg() { result = this.getArgument(0) }

  Expr getSizeArg() { result = this.getArgument(1) }
}

Type strip(Type t) { result = t.getUnspecifiedType() }

predicate typeIsFlexibleStruct(Type t, FlexibleArrayStruct fas) {
  strip(t) = fas
  or
  strip(t).(PointerType).getBaseType().getUnspecifiedType() = fas
  or
  exists(TypedefType ta |
    strip(t) = ta and strip(ta.getBaseType()) = fas
  )
  or
  exists(TypedefType ta |
    strip(t).(PointerType).getBaseType().getUnspecifiedType() = ta and
    strip(ta.getBaseType()) = fas
  )
}

predicate sizeIsOnlyFixedHeader(Expr sizeArg, FlexibleArrayStruct fas) {
  exists(SizeofTypeOperator s |
    s = sizeArg and typeIsFlexibleStruct(s.getTypeOperand(), fas)
  )
  or
  exists(SizeofExprOperator s |
    s = sizeArg and typeIsFlexibleStruct(s.getExprOperand().getType(), fas)
  )
  or
  sizeIsOnlyFixedHeader(sizeArg.(Conversion).getExpr(), fas)
  or
  sizeIsOnlyFixedHeader(sizeArg.(ParenthesisExpr).getExpr(), fas)
}

predicate bufferIsFlexibleStruct(Expr buf, FlexibleArrayStruct fas) {
  typeIsFlexibleStruct(buf.getType(), fas)
  or
  typeIsFlexibleStruct(buf.(Cast).getType(), fas)
  or
  bufferIsFlexibleStruct(buf.(Conversion).getExpr(), fas)
  or
  bufferIsFlexibleStruct(buf.(ParenthesisExpr).getExpr(), fas)
}

/**
 * The same function also byteswaps the FAM field (correct split header/FAM
 * pattern). Suppress those.
 */
predicate famIsByteswappedSeparately(ByteswapCall headerSwap, FlexibleArrayStruct fas) {
  exists(ByteswapCall famSwap, FieldAccess fa |
    famSwap != headerSwap and
    famSwap.getEnclosingFunction() = headerSwap.getEnclosingFunction() and
    fa.getTarget() = fas.getFam() and
    fa.getEnclosingElement*() = famSwap.getBufferArg()
  )
}

from ByteswapCall c, FlexibleArrayStruct fas, string tname
where
  fas.hasStructuredFam() and
  tname = min(string n | n = fas.getDisplayName() | n) and
  sizeIsOnlyFixedHeader(c.getSizeArg(), fas) and
  bufferIsFlexibleStruct(c.getBufferArg(), fas) and
  not famIsByteswappedSeparately(c, fas)
select c,
  "byteswap of sizeof(" + tname +
    ") does not convert flexible array member '" + fas.getFam().getName() +
    "' (element type " + fas.getFamElementType().getName() + ") in " +
    c.getEnclosingFunction().getName() + "."
