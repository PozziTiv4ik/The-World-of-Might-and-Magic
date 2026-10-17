// Тесты SHA-256: эталонные значения FIPS 180-4 (пустая строка, один блок, хвост на два блока).
#include <cstring>

#include "codec/sha256.h"
#include "tests/test.h"

using namespace rg;

TEST(codec_sha256_fips_vectors) {
  CHECK_EQ(codec::sha256Hex("", 0), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
  CHECK_EQ(codec::sha256Hex("abc", 3), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
  const char* two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  CHECK_EQ(codec::sha256Hex(two, std::strlen(two)), std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
}
