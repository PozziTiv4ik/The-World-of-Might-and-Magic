// Тесты base: числа и сроки по-русски (fmtNum, nTurns, parseNum), UTF-8, поиск без учёта регистра и «ё»,
// сравнение строк по-русски.
#include "tests/test.h"

using namespace rg;

TEST(base_format_ru) {
  CHECK_EQ(fmtNum(1234567), std::string("1\xE2\x80\xAF" "234\xE2\x80\xAF" "567"));
  CHECK_EQ(fmtNum(1234), std::string("1234"));
  CHECK_EQ(fmtNum(-5.5, 1), std::string("\xE2\x88\x92" "5,5"));
  CHECK_EQ(fmtSigned(3), std::string("+3"));
  CHECK_EQ(nTurns(3), std::string("3 хода"));
  CHECK_EQ(nTurns(11), std::string("11 ходов"));
  CHECK_EQ(nTurns(21), std::string("21 ход"));
  CHECK(parseNum("1 234,5").value() == 1234.5);
  CHECK(!parseNum("abc").has_value());
}

TEST(base_utf8_and_search) {
  std::string s = "Ёжик в Тумане";
  CHECK_EQ(utf8::count(s), size_t(13));
  CHECK_EQ(utf8::lower(s), std::string("ёжик в тумане"));
  CHECK(utf8::matches(s, "ежик туман"));
  CHECK(!utf8::matches(s, "лес"));
  CHECK(compareRu("Арден", "беррин") < 0);
  CHECK(compareRu("ель", "ёж") < 0);
  CHECK(compareRu("ёж", "жук") < 0);
  CHECK(compareRu("Провинция 2", "Провинция 10") < 0);
  CHECK_EQ(utf8::fromWide(utf8::toWide("Мир 🌍")), std::string("Мир 🌍"));
}
