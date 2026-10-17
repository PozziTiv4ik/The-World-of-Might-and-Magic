// Regnum — связь сущностей мира с каноном кампании (карточки 03_Персонажи и 04_Локации рядом с проектом):
// поиск карточки по части названия, имени файла и псевдонимов (aliases), подтверждение («Точно ли это…») или выбор
// из нескольких найденных с уточняющим поиском, заметки из раздела «Кратко», портрет героя из поля portrait
// карточки (ТЗ «Фиксы», п.5–8; «Исправления», п.1). Реликвии — карточки активов-предметов 05_Активы_персонажей
// (type: character_asset, asset_kind: item): описание из «Кратко», изображение из поля visual (ТЗ «Доработки №1», п.14).
#pragma once
#include "app/app.h"

namespace rg::app::canon {

enum class Kind : u8 { Character, Location, Relic };

struct Card {
  std::string id, title, path;           // ID карточки (CHAR-0001, LOC-0020), заголовок, путь к файлу
  std::string type;                      // поле type карточки
  std::vector<std::string> aliases;
  std::string portrait;                  // путь портрета от корня проекта (персонажи; у актива — поле visual)
  std::string assetKind;                 // поле asset_kind карточки актива (item — предмет)
};

// Корень проекта кампании (папка с 03_Персонажи / 04_Локации) выше папки мира или рабочей папки; пусто — не найден.
std::string projectRoot(App& a);
// Карточки по части строки (без учёта регистра и ё/е) в заголовке, имени файла, псевдонимах и ID: «эйганн» находит
// «Капитан Эйганн». Порядок: точные совпадения, затем начинающиеся с запроса, затем содержащие его, затем
// содержащие все слова запроса; внутри — по заголовку.
std::vector<Card> find(App& a, Kind kind, std::string_view name);
std::optional<Card> byId(App& a, Kind kind, std::string_view id);
// Текст раздела «Кратко» карточки (иначе — первый абзац).
std::string summary(const Card& c);

// Раздел «Канон» инспектора: ID карточки, поиск по названию (вопрос или выбор), открыть, отвязать.
// type — Character, Province или Faction.
void section(App& a, SelType type, Id id);
// Заметки: при связи с каноном — только чтение (ТЗ «Фиксы», п.8).
void notesField(App& a, SelType type, Id id, float height = 96);
// Связать сущность с карточкой: entity, заметки из «Кратко», портрет героя.
void link(App& a, SelType type, Id id, const Card& card);

// ---- реликвии (ТЗ «Доработки №1», п.14)
// Раздел «Канон» карточки реликвии: ID карточки актива (ASSET-…), поиск по названию, открыть, отвязать.
void relicSection(App& a, Id relic);
// Описание реликвии: при связи с каноном — только чтение (обновление из карточки).
void relicDescField(App& a, Id relic, float height = 96);
// Связать реликвию с карточкой: entity, описание из «Кратко», изображение из визуала карточки (если он есть).
void linkRelic(App& a, Id relic, const Card& card);
// Изображение для значка: PNG/JPEG как есть или уменьшенное до maxSide (PNG); пусто — не изображение.
std::string fitImage(const std::string& bytes, int maxSide);

}  // namespace rg::app::canon
