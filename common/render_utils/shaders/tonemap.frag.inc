#ifndef TONEMAP_FRAG_H_INCLUDED
#define TONEMAP_FRAG_H_INCLUDED

#include "tonemap.h"

#define ENCODE_TONEMAP_RESULT(ldr_) \
  GAMMA_ENCODE_COND((ldr_), constants.gammaEncodeInTonemapping != 0)
#define ENCODE_AA_RESULT(ldr_) \
  GAMMA_ENCODE_COND((ldr_), constants.gammaEncodeInTonemapping == 0)

#endif // TONEMAP_FRAG_H_INCLUDED

