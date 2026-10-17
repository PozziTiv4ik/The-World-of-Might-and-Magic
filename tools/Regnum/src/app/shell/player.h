// Regnum — плеер саундтреков в верхней строке (shell/player.cpp): треки папки assets/soundtracks, движок
// platform::audio, настройки в app.json (объект «player»).
#pragma once
#include "app/shell/shell_internal.h"
#include "base/json.h"
#include "platform/audio.h"

namespace rg::app::detail::shell {

// Плеер приложения (из loadPrefs при создании App): новый движок, настройки prefs (громкость, без звука, перемешивание,
// повтор, последний трек; null — по умолчанию) и разбор папки саундтреков в фоне. Воспроизведение не начинается.
void playerAttach(App& a, const json::Value& prefs);
// Настройки плеера для app.json (savePrefs); null — плеера нет.
json::Value playerPrefs();
// Раскладка кадра: ширина плеера в верхней строке. free — середина строки без плеера (там же параметры инструмента:
// плеер уступает им место и оставляет только кнопки), narrow — узкое окно (только кнопки).
float playerLayout(App& a, float free, bool narrow);
// Плеер в прямоугольнике r верхней строки (ширина — из playerLayout этого кадра).
void playerBar(App& a, RectF r);
// Папка саундтреков: assets/soundtracks рядом с программой или от текущей папки (map::findAssetsDir); пусто — нет.
std::string soundtracksDir();
// Другая папка саундтреков (тесты): разбор заново, текущим станет последний трек из настроек.
void playerUseFolder(const std::string& dir);
platform::audio::Player* playerEngine();   // nullptr — плеер не создан

}  // namespace rg::app::detail::shell
