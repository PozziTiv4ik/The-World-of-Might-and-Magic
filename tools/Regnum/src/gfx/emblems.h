// Regnum — геральдические эмблемы для флагов: залитые силуэты на сетке 100 × 100.
//
// Эмблема рисуется одним цветом по центру квадрата rect (сторона — min(w, h)); детали — вырезы в силуэте.
// Механизм тот же, что у значков (gfx/glyph.h): маски кешируются, мелкие размеры подгоняются к пикселям.
#pragma once
#include "gfx/icons.h"

namespace rg::gfx {

// Имена эмблем в порядке каталога (имена хранятся в файлах миров — не переименовывать).
// Классические: crown, tower, castle, star, sun, moon, tree, lily, sword, swords, shield, skull, anchor, ship, wheat,
// gem, hammer, axe, bow, key, eye, flame, eagle, lion, dragon, wolf, bull, horse, serpent, kraken, rune, rose,
// griffin, bear.
// Драконы: dragon-wings, dragon-coiled, dragon-eye, dragon-skull.
// Вампиры: vampire-bat, vampire-fangs, vampire-chalice, vampire-coffin.
// Природа: nature-leaf, nature-stag, nature-mushroom, nature-pine.
// Стихии: element-fire, element-water, element-air, element-earth, element-lightning, element-frost.
// Гномы: dwarf-anvil, dwarf-pickaxes, dwarf-face, dwarf-gate.
// Эльфы: elf-leaf, elf-star, elf-wreath, elf-swan.
// Некроманты: necro-crossbones, necro-scythe, necro-hand, necro-tomb.
// Тёмные эльфы: dark-elf-spider, dark-elf-web, dark-elf-daggers, dark-elf-crescent.
// Наги: naga-cobra, naga-trident, naga-shell, naga-serpents.
// Рыцари: knight-helm, knight-chess, knight-gauntlet, knight-shield.
// Святые: holy-cross, holy-wings, holy-dove, holy-grail.
// Демоны: demon-head, demon-imp, demon-claw, demon-pitchfork.
// Культисты: cult-pentagram, cult-hood, cult-eye, cult-goat.
// Магия: magic-hat, magic-book, magic-orb, magic-staff, magic-potion.
// Орки: orc-head, orc-boar, orc-cleavers, orc-totem.
// Ящеры: lizard-crawl, lizard-head, lizard-croc, lizard-pyramid, lizard-track.
// Зверолюды: beast-minotaur, beast-centaur, beast-satyr, beast-harpy, beast-mermaid.
const std::vector<std::string>& emblemNames();
bool hasEmblem(std::string_view name);
// Подпись для выбора эмблемы («Корона»); пустая строка — неизвестное имя.
std::string_view emblemTitle(std::string_view name);
// Нарисовать эмблему; пустое имя — ничего, неизвестное — ничего и одна запись в журнал на имя.
void drawEmblem(Canvas& c, std::string_view name, RectF rect, Color color);

// Раздел выбора эмблемы: темы (классические и фантазийные группы) и расы (группа на расу справочника).
enum class EmblemSection : u8 { Themes = 0, Races = 1 };
// Группа для выбора эмблемы: название («Драконы», «Люди») и имена в порядке показа.
struct EmblemGroup {
  std::string_view title;
  std::vector<std::string> names;
  EmblemSection section = EmblemSection::Themes;
};
// Группы каталога: «Классические», фантазийные (по списку выше), затем группы рас (раздел Races, по алфавиту, имена
// race-<раса>-…). Каждая эмблема — ровно в одной группе; названия групп различны внутри раздела.
const std::vector<EmblemGroup>& emblemGroups();

const VecGlyph* emblemGlyph(std::string_view name);
// Проблемы реестра (ошибки путей, выход за сетку, подписи, группы) — пусто, если всё в порядке.
std::vector<std::string> emblemRegistryIssues();

}  // namespace rg::gfx
