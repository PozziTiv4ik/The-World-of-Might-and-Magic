// Regnum — реликвии в интерфейсе (ТЗ «Доработки №4», п.3–4): легендарные и эпохальные уникальны, эпические и ниже
// лежат стопками с количеством в инвентарях героев, государств и хранилищах построек. Общее для инвентаря героя,
// инвентаря государства (вкладка «Археология»), хранилища постройки, вкладки «Герои», тайника провинции и справочника
// реликвий: места всех реликвий за один проход по миру, подписи мест, строка стопки «× N», поле количества
// администратора, источники выдачи, окна «куда и сколько» и «сколько», переход в справочник.
#pragma once
#include <unordered_map>

#include "app/app.h"

namespace rg::app::relics {

using rules::RelicAt;
using rules::RelicPlace;

// Места всех реликвий мира с количеством за один проход (порядок мест — как rules::relicPlaces: герои, тайники и
// хранилища провинций, инвентари государств).
class Places {
 public:
  explicit Places(const World& w);
  const std::vector<RelicAt>& of(Id relic) const;
  i64 total(Id relic) const;            // штук во всех местах
 private:
  std::unordered_map<Id, std::vector<RelicAt>> at_;
};

// Подпись места: текст, значок, цвет фракции, куда перейти щелчком (пусто — некуда), подсказка.
struct PlaceView {
  std::string text;
  const char* icon = "relic";
  Color color{0, 0, 0, 0};
  Selection go;
  std::string tip;
};
PlaceView placeView(const World& w, const RelicPlace& p);
// Фишка места (с переходом по щелчку); count > 1 — «· × N». В ChipFlow — с переносом.
void placeChip(App& a, const World& w, const RelicPlace& p, i64 count = 0);

// «× 12» — количество штук в подписях.
std::string times(i64 n);
// Штук во всех стопках (только реликвии справочника).
i64 pieces(const World& w, const std::vector<RelicStack>& v);
// Стопки по главенству редкости, затем по названию (реликвии вне справочника пропущены).
std::vector<RelicStack> sorted(const World& w, const std::vector<RelicStack>& v);
// Подпись под названием: редкость и «уникальная» (легендарная и эпохальная).
std::string kindText(const Relic& r);

// Строка стопки: значок, название цветом редкости, подпись sub (пусто — редкость); при count > 1 справа — «× N».
// Высота 40; справа остаётся right точек под кнопки вызывающего; row — прямоугольник строки. true — щелчок по строке.
bool stackRow(const Relic& r, i64 count, std::string_view sub = {}, float right = 0, RectF* row = nullptr);
constexpr float kCountW = 104;          // ширина поля количества (countField)
// Количество для администратора (правка мира): поле с «−» и «+» в прямоугольнике r. true — изменено (value — новое).
bool countField(std::string_view id, RectF r, i64& value, i64 min = 0);

// Откуда выдать реликвию герою или положить в хранилище: место (Free — новая из справочника) и сколько там штук.
struct Source {
  Id relic = 0;
  RelicPlace from;
  i64 count = 0;
  int rank = 0;    // 0 — инвентарь государства, 1 — хранилища государства, 2 — его герои, 3 — справочник, 4 — чужие места
};
enum class Target : u8 { Hero, Store };
// Источники для места to государства state по правилам rules::giveRelic и rules::storeRelic: владения государства
// (кроме самого места to); admin — ещё свободные из справочника и (для героя) места других фракций. По рангу, затем по
// главенству редкости и названию.
std::vector<Source> sources(const World& w, const Places& pl, Id state, const RelicPlace& to, Target kind, bool admin);
// Выбор источника из списка (поиск; подпись справа — где лежит и сколько). −1 — ничего не выбрано в этом кадре.
int sourceCombo(std::string_view id, const World& w, Id state, const std::vector<Source>& src, std::string_view placeholder,
                std::string_view tooltip, bool disabled = false);

// Цель окна «куда и сколько».
struct Dest {
  std::string label, hint;
  const char* icon = nullptr;
  Color color{0, 0, 0, 0};
  bool disabled = false;
};
// Всплывающее окно у последнего элемента (открывает вызывающий: ui::openPopup(id)): заголовок, при max > 1 — поле
// «Сколько» (1…max), строки целей (метки <mark>.count, <mark>.<i>). Возвращает выбранную цель (−1 — нет), count — штук.
int destPopup(App& a, std::string_view id, std::string_view header, const std::vector<Dest>& dests, i64 max, i64& count,
              std::string_view mark);
// Окно «Сколько» (1…max) с кнопкой okLabel (метки <mark>.count, <mark>.ok). true — подтверждено, count — штук.
bool countPopup(App& a, std::string_view id, std::string_view header, std::string_view okLabel, i64 max, i64& count,
                std::string_view mark);

// Справочник реликвий (только администратор): вкладка «Реликвии», запись выбрана и показана (editors/catalogs_relics.cpp).
void openCatalog(App& a, Id relic);

}  // namespace rg::app::relics
