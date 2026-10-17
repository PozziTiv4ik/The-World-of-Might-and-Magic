// Regnum — SHA-256 (контрольная сумма исходника карты).
#pragma once
#include "base/base.h"

namespace rg::codec {

// SHA-256 в нижнем регистре (64 шестнадцатеричные цифры).
std::string sha256Hex(const void* data, size_t size);

}  // namespace rg::codec
