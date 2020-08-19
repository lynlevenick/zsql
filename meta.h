#ifndef Z_META_H
#define Z_META_H

#define META__CONCAT2(_0, _1) _0##_1
#define META_CONCAT2(_0, _1) META__CONCAT2(_0, _1)
#define META_CONCAT3(_0, _1, _2) META_CONCAT2(META_CONCAT2(_0, _1), _2)
#define META_CONCAT4(_0, _1, _2, _3) META_CONCAT2(META_CONCAT3(_0, _1, _2), _3)
#define META_CONCAT5(_0, _1, _2, _3, _4)                                       \
  META_CONCAT2(META_CONCAT4(_0, _1, _2, _3), _4)

#define META__ARG(                                                             \
    _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16,     \
    _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, \
    _32, _33, _34, _35, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, \
    _47, _48, _49, _50, _51, _52, _53, _54, _55, _56, _57, _58, _59, _60, _61, \
    _62, _63, _64, _65, _66, _67, _68, _69, _70, _71, _72, _73, _74, _75, _76, \
    _77, _78, _79, _80, _81, _82, _83, _84, _85, _86, _87, _88, _89, _90, _91, \
    _92, _93, _94, _95, _96, _97, _98, _99, _100, _101, _102, _103, _104,      \
    _105, _106, _107, _108, _109, _110, _111, _112, _113, _114, _115, _116,    \
    _117, _118, _119, _120, _121, _122, _123, _124, _125, _126, _127, _128,    \
    _129, _130, _131, _132, _133, _134, _135, _136, _137, _138, _139, _140,    \
    _141, _142, _143, _144, _145, _146, _147, _148, _149, _150, _151, _152,    \
    _153, _154, _155, _156, _157, _158, _159, ...)                             \
  _159
#define META__NARG2(...)                                                       \
  META__ARG(__VA_ARGS__, 158, 157, 156, 155, 154, 153, 152, 151, 150, 149,     \
            148, 147, 146, 145, 144, 143, 142, 141, 140, 139, 138, 137, 136,   \
            135, 134, 133, 132, 131, 130, 129, 128, 127, 126, 125, 124, 123,   \
            122, 121, 120, 119, 118, 117, 116, 115, 114, 113, 112, 111, 110,   \
            109, 108, 107, 106, 105, 104, 103, 102, 101, 100, 99, 98, 97, 96,  \
            95, 94, 93, 92, 91, 90, 89, 88, 87, 86, 85, 84, 83, 82, 81, 80,    \
            79, 78, 77, 76, 75, 74, 73, 72, 71, 70, 69, 68, 67, 66, 65, 64,    \
            63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48,    \
            47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32,    \
            31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16,    \
            15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, )
#define META_NARG(...) META__NARG2(__VA_ARGS__)

#define META_HAS_COMMA(...)                                                    \
  META__ARG(__VA_ARGS__, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, \
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  \
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  \
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  \
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  \
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  \
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  \
            1, 1, 1, 1, 1, 1, 1, 0, 0)

#define META__TRIGGER_PARENTHESIS(...) ,

#define META__IS_EMPTY(_0, _1, _2, _3)                                         \
  META_HAS_COMMA(META_CONCAT5(META__IS_EMPTY_, _0, _1, _2, _3))
#define META__IS_EMPTY_0001 ,
#define META_IS_EMPTY(...)                                                     \
  META__IS_EMPTY(META_HAS_COMMA(__VA_ARGS__),                                  \
                 META_HAS_COMMA(META__TRIGGER_PARENTHESIS __VA_ARGS__),        \
                 META_HAS_COMMA(__VA_ARGS__()),                                \
                 META_HAS_COMMA(META__TRIGGER_PARENTHESIS __VA_ARGS__()))

#define META__IF_SKIP(...)
#define META__IF_EXEC(...) __VA_ARGS__
#define META__IF_EXPAND1(...) META__IF_EXEC
#define META__IF_EXPAND2(...) __VA_ARGS__ META__IF_SKIP

#define META__IS_0_EQ_0(...) ,
#define META__IS_1_EQ_1(...) ,

#define META__IF_CLAUSE2(_0, _1, _2, ...) _2
#define META__IF_CLAUSE(_0)                                                    \
  META__IF_CLAUSE2(_0, META__IF_EXPAND2, META__IF_EXPAND1, ~)
#define META_IF_EQ(_0, _1)                                                     \
  META__IF_CLAUSE(META_CONCAT4(META__IS_, _0, _EQ_, _1)())
#define META_IF_EQ_0(_0) META_IF_EQ(0, _0)
#define META_IF_EQ_1(_0) META_IF_EQ(1, _0)

#define META__TOK_EQ(TOK, ...)                                                 \
  META__TOK_EQ2(META_CONCAT3(META__IS_, TOK, _EQ_), __VA_ARGS__)
#define META__TOK_EQ2(MAC, ...) META__TOK_EQ3(MAC, __VA_ARGS__)
#define META__TOK_EQ3(MAC, ...)                                                \
  META_HAS_COMMA(META_CONCAT2(                                                 \
      META__TOK_EQ4_, META_NARG(MAC##__VA_ARGS__(~) MAC##__VA_ARGS__))(~))
#define META__TOK_EQ4_2(...) ,

#define META_IS_EQ_0(...) META__TOK_EQ(0, __VA_ARGS__)
#define META_IS_EQ_1(...) META__TOK_EQ(1, __VA_ARGS__)

#define META_IF_ELSE(...) META_IF_EQ_0(META_IS_EQ_0(__VA_ARGS__))
#define META_IF_EMPTY(...) META_IF_EQ_1(META_IS_EMPTY(__VA_ARGS__))

#endif
