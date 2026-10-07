// Подсказки с полным текстом у подписей, обрезанных многоточием: подпись и кнопка, однострочное поле без фокуса
// (и недоступное), выбранный пункт выпадающего списка и пункты открытого списка, ячейка таблицы (щелчок по ней
// по-прежнему выделяет строку), заголовок столбца; своя подсказка элемента — под полным текстом; текст, который
// поместился, подсказки не даёт.
#include "tests/test_ui_util.h"
#include "ui/ui_internal.h"

using namespace rg;
using namespace rg::uitest;

namespace {

const char* kLong = "Тяжёлая кавалерия северных баронов Хельдвига";

// Подсказка, показанная после наведения на (x, y) и ожидания дольше задержки; пусто — подсказки нет.
std::string tipAt(H& h, float x, float y) {
  h.move(700, 560);   // увести указатель: подсказка элемента начинается заново
  h.drain();
  h.move(x, y);
  h.drain();
  h.wait(0.5);
  h.frames(3);
  const ui::in::Ctx& c = ui::in::C();
  return c.tipRequested ? c.tipText : std::string();
}

}  // namespace

TEST(ui_fulltext_text_field) {
  H h;
  std::string longV = kLong, shortV = "Стрелки", owned = kLong, locked = kLong;
  RectF rl, rs, ro, rd;
  h.build = [&] {
    ui::Area a({20, 20, 180, 400});
    ui::textField("long", longV);
    rl = ui::lastItem().rect;
    ui::textField("short", shortV);
    rs = ui::lastItem().rect;
    ui::textField("owned", owned, {.tooltip = "Наименование"});
    ro = ui::lastItem().rect;
    ui::Disabled dis(true);
    ui::textField("locked", locked);
    rd = ui::lastItem().rect;
  };
  h.frame();
  CHECK_EQ(tipAt(h, rl.cx(), rl.cy()), std::string(kLong));
  CHECK(h.save("fulltext_field"));
  CHECK(tipAt(h, rs.cx(), rs.cy()).empty());                                   // поместилось — без подсказки
  CHECK_EQ(tipAt(h, ro.cx(), ro.cy()), std::string(kLong) + "\nНаименование");  // своя подсказка — под текстом
  CHECK_EQ(tipAt(h, rd.cx(), rd.cy()), std::string(kLong));                    // недоступное поле — тоже
  // В фокусе (правка) — без подсказки: текст прокручивается в самом поле.
  h.click(rl.cx(), rl.cy());
  h.drain();
  CHECK(tipAt(h, rl.cx(), rl.cy()).empty());
}

TEST(ui_fulltext_combo) {
  H h;
  const std::vector<std::string> labels = {kLong, "Стрелки", "Колдующие отряды северных провинций королевства"};
  std::vector<ui::Option> opts;
  for (const std::string& s : labels) opts.push_back(ui::Option{s, "army"});
  int idx = 0, idx2 = 1;
  RectF rc, rs;
  h.build = [&] {
    ui::Area a({20, 20, 200, 500});
    ui::combo("units", idx, opts);
    rc = ui::lastItem().rect;
    ui::combo("short", idx2, opts, {.tooltip = "Тип войск"});
    rs = ui::lastItem().rect;
  };
  h.frame();
  CHECK_EQ(tipAt(h, rc.cx(), rc.cy()), std::string(kLong));
  CHECK_EQ(tipAt(h, rs.cx(), rs.cy()), std::string("Тип войск"));   // поместилось — своя подсказка
  // Открытый список: обрезанный пункт — подсказка с полной подписью.
  h.click(rc.cx(), rc.cy());
  h.drain();
  const float rowY = rc.bottom() + 5 + 30 * 2 + 15;   // третий пункт (отступ окна 5, строки по 30)
  CHECK_EQ(tipAt(h, rc.cx(), rowY), labels[2]);
  CHECK(h.save("fulltext_combo_list"));
}

TEST(ui_fulltext_table) {
  H h;
  int sel = -1;
  RectF cellR;
  h.build = [&] {
    ui::Area a({20, 20, 360, 400});
    ui::Column cols[] = {{"Наименование отряда в таблице", nullptr, ui::px(110)}, {"Всего", nullptr, ui::fr(1), ui::Align::Right}};
    ui::Table t("rows", cols, 2, {.selected = &sel});
    for (int i : t) {
      t.text(i == 0 ? kLong : "Стрелки");
      t.text(i == 0 ? "700" : "600");
      const RectF rr = t.rowRect();
      if (i == 0) cellR = RectF{rr.x + 10, rr.y + 2, 90, rr.h - 4};   // первая ячейка (столбец 110 без отступов)
    }
  };
  h.frame();
  CHECK_EQ(tipAt(h, cellR.cx(), cellR.cy()), std::string(kLong));
  CHECK(h.save("fulltext_table"));
  // Щелчок по обрезанной ячейке выделяет строку (подсказка не перехватывает нажатие).
  h.click(cellR.cx(), cellR.cy());
  CHECK_EQ(sel, 0);
  // Заголовок, не поместившийся в столбец, — подсказка с полным заголовком.
  CHECK_EQ(tipAt(h, 30 + 40, 20 + 16), std::string("Наименование отряда в таблице"));
}

// Подсказка поверх строки таблицы для своей отрисовки (ui::hoverTip): не перехватывает наведение и щелчок строки.
TEST(ui_fulltext_hover_tip) {
  H h;
  int sel = -1;
  RectF cellR;
  h.build = [&] {
    ui::Area a({20, 20, 360, 400});
    ui::Column cols[] = {{"Отряд", nullptr, ui::fr(1)}};
    ui::Table t("rows", cols, 3, {.selected = &sel});
    for (int i : t) {
      const RectF cr = t.cell();
      ui::draw::text("Своя отрисовка", cr, ui::Font::Body, ui::theme().text);
      ui::hoverTip("##tip", cr, "Полный текст " + std::to_string(i));
      if (i == 1) cellR = cr;
    }
  };
  h.frame();
  CHECK_EQ(tipAt(h, cellR.cx(), cellR.cy()), std::string("Полный текст 1"));
  h.click(cellR.cx(), cellR.cy());
  CHECK_EQ(sel, 1);
}

// Подпись и кнопка: обрезанная многоточием — полный текст в подсказке (у кнопки — над своей подсказкой); подпись, которая
// поместилась, подсказки не даёт; щелчок по обрезанной кнопке по-прежнему нажимает её.
TEST(ui_fulltext_label_button) {
  H h;
  RectF rl, rs, rb, rt;
  int clicks = 0;
  h.build = [&] {
    ui::Area a({20, 20, 160, 400});
    ui::label(kLong);
    rl = ui::lastItem().rect;
    ui::label("Стрелки");
    rs = ui::lastItem().rect;
    if (ui::button(kLong, {.fill = true})) ++clicks;
    rb = ui::lastItem().rect;
    ui::button("Колдующие отряды северных провинций", {.fill = true, .tooltip = "Сформировать"});
    rt = ui::lastItem().rect;
  };
  h.frame();
  CHECK_EQ(tipAt(h, rl.cx(), rl.cy()), std::string(kLong));
  CHECK(tipAt(h, rs.x + 4, rs.cy()).empty());
  CHECK_EQ(tipAt(h, rb.cx(), rb.cy()), std::string(kLong));
  CHECK_EQ(tipAt(h, rt.cx(), rt.cy()), std::string("Колдующие отряды северных провинций\nСформировать"));
  CHECK(h.save("fulltext_label_button"));
  h.click(rb.cx(), rb.cy());
  CHECK_EQ(clicks, 1);
}
