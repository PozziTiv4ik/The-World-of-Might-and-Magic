// Regnum — страницы на месте карты: редактор, страница сущности (выделение страницей) и каталог раздела.
// Слева — навигация: «← К карте» (или «← Назад» к странице под редактором), пункты раздела: редакторы группы,
// у сущности — каталог раздела и её вкладки; справа — содержимое на фоне страницы.
#include "app/shell_internal.h"

namespace rg::app::detail::shell {

using platform::Key;

namespace {

// Что под редактором: страница сущности, каталог или карта.
View beneathEditor(App& a) {
  if (!a.ui.directory.empty()) return View::Directory;
  if (a.ui.page && a.ui.sel) return View::Entity;
  return View::Map;
}

std::vector<const TabDef*> visibleTabs(App& a, Selection sel) {
  std::vector<const TabDef*> list;
  for (auto& t : tabs()) {
    if (t.type != sel.type || !t.draw) continue;
    bool vis = true;
    if (t.visible) {
      try {
        vis = t.visible(a, sel.id);
      } catch (const std::exception& e) {
        a.error(e);
        vis = false;
      }
    }
    if (vis) list.push_back(&t);
  }
  return list;
}

const TabDef* currentTab(App& a, Selection sel, const std::vector<const TabDef*>& list) {
  if (list.empty()) return nullptr;
  std::string& cur = a.ui.tabOf[sel.type];
  for (auto* t : list)
    if (cur == t->id) return t;
  cur = list.front()->id;
  return list.front();
}

// «← К карте» / «← Назад».
void backItem(App& a, bool toPage) {
  const char* label = toPage ? "Назад" : "К карте";
  if (ui::listItem(std::string(label) + "##back", {.icon = "arrow-left", .tooltip = toPage ? "Назад · Esc" : "К карте · Esc"})) {
    if (toPage) later(a, [](App& x) { x.back(); });
    else later(a, [](App& x) { x.toMap(); });
  }
  a.markUi("page.back");
  a.markUi("editor.back");
}

// ---------------------------------------------------------------- навигация
void navEditors(App& a, const EditorDef& cur) {
  if (cur.group) {
    std::vector<const EditorDef*> list;
    for (auto& e : editors())
      if (e.group && std::string_view(e.group) == cur.group) list.push_back(&e);
    std::stable_sort(list.begin(), list.end(), [](const EditorDef* x, const EditorDef* y) { return x->order < y->order; });
    if (const SectionDef* sec = findSection(cur.group)) navCaption(sec->title);
    for (const EditorDef* e : list) {
      ui::IdScope s(e->id);
      bool active = e == &cur;
      if (ui::listItem(e->title, {.icon = e->icon ? e->icon : "file", .selected = active}) && !active) {
        std::string id = e->id;
        later(a, [id](App& x) { x.openEditor(id, 0); });
      }
      a.markUi(std::string("page.nav.") + e->id);
    }
  } else {
    ui::listItem(std::string(cur.title) + "##cur", {.icon = cur.icon ? cur.icon : "file", .selected = true});
    a.markUi(std::string("page.nav.") + cur.id);
  }
}

void navEntity(App& a, Selection sel, const std::vector<const TabDef*>& list, const TabDef* cur) {
  if (const SectionDef* sec = a.sectionOf(sel); sec && sec->allTitle) {
    if (ui::listItem(sec->allTitle, {.icon = "list"})) {
      std::string id = sec->id;
      later(a, [id](App& x) { x.openDirectory(id); });
    }
    a.markUi("page.directory");
    ui::gap(6);
    ui::spacer(2);
  }
  std::string cap = selCaption(sel.type);
  if (sel.type == SelType::Faction)
    if (const Faction* f = a.world().faction(sel.id)) cap = f->isGuild() ? "Гильдия" : "Государство";
  navCaption(cap);
  for (const TabDef* t : list) {
    ui::IdScope s(t->id);
    int badge = 0;
    if (t->badge) {
      try {
        badge = t->badge(a, sel.id);
      } catch (...) {
        badge = 0;
      }
    }
    std::string b = badge > 0 ? std::to_string(badge) : std::string();
    if (ui::listItem(t->title, {.icon = t->icon, .badge = b, .selected = t == cur})) a.ui.tabOf[sel.type] = t->id;
    if (badge < 0) {
      RectF r = ui::lastItem().rect;
      ui::draw::circle(r.right() - 14, r.cy(), 3.5f, ui::theme().danger);
    }
    a.markUi(std::string("page.tab.") + t->id);
  }
}

void navDirectory(App& a, const SectionDef& sec) {
  navCaption(sec.title);
  ui::listItem(std::string(sec.allTitle ? sec.allTitle : sec.title) + "##dir", {.icon = "list", .selected = true});
  a.markUi("page.directory");
}

// ---------------------------------------------------------------- содержимое
// Колонка содержимого: на широком экране — не шире kPageCol, по середине (поля и списки не растягиваются на всю
// ширину); вкладка TabDef::wide — во всю ширину.
RectF column(RectF r, bool wide, float maxW = kPageCol, float pad = 24) {
  RectF in = r.inset(pad, 0);
  if (!wide && in.w > maxW) {
    in.x = std::round(in.x + (in.w - maxW) * 0.5f);
    in.w = maxW;
  }
  return in;
}

void editorContent(App& a, const EditorDef& ed, RectF r) {
  a.markUi("editor", r);
  ui::Area body(r, 16);
  ui::IdScope s(ed.id);
  try {
    ed.draw(a, a.ui.editorArg);
  } catch (const std::exception& e) {
    a.error(e);
  }
}

void entityContent(App& a, Selection sel, const TabDef* tab, RectF r) {
  const World& w = a.world();
  const ui::Theme& th = ui::theme();
  const bool wide = tab && tab->wide;
  RectF col = column(r, wide);
  // Действия страницы — в правом верхнем углу: показать на карте, свернуть в панель, закрыть.
  {
    const float actW = 3 * 30 + 2 * 4;
    ui::Area ar(RectF{r.right() - 20 - actW, r.y + 14, actW, 30}, 0);
    ui::HStack hs(30, ui::Align::Left, 4);
    if (ui::iconButton("target", "Показать на карте")) later(a, [](App& x) {
        x.setPage(false);
        x.impl().focusAfter = 2;
      });
    ui::tooltip("Показать на карте", parseShortcut("F"));
    a.markUi("inspector.focus");
    if (ui::iconButton("minimize", "Свернуть в панель")) later(a, [](App& x) { x.setPage(false); });
    a.markUi("inspector.collapse");
    if (ui::iconButton("close", "Закрыть")) later(a, [](App& x) { x.toMap(); });
    ui::tooltip("Закрыть", {Key::Escape, 0});
    a.markUi("inspector.close");
  }
  // Шапка: шапки типа (флаг, название, правитель…) — в колонке обычной ширины, на месте при смене вкладок.
  float headBottom = r.y;
  {
    const RectF hc = column(r, false);
    float hw = std::min(hc.w, std::max(0.f, r.right() - 20 - 3 * 30 - 8 - 16 - hc.x));
    ui::Area ar(RectF{hc.x, r.y + 16, hw, r.h}, 0);
    {
      ui::Group g(0, 4);
      ui::IdScope scope{int(sel.type)};
      bool any = false;
      for (auto& h : headers()) {
        if (h.type != sel.type || !h.draw) continue;
        any = true;
        ui::IdScope hs(h.id);
        try {
          h.draw(a, sel.id);
        } catch (const std::exception& e) {
          a.error(e);
        }
      }
      if (!any) {
        ui::caption(selCaption(sel.type));
        ui::label(entityName(w, sel), {.font = ui::Font::Display});
      }
    }
    headBottom = ui::avail().y;
  }
  headBottom = std::round(headBottom + 4);
  ui::draw::line(r.x, headBottom, r.right(), headBottom, th.border, 1);
  // Как у панели выделения: «inspector» — вся страница с шапкой, «inspector.tabs» — до линии под шапкой.
  a.markUi("inspector.tabs", RectF{r.x, r.y, r.w, headBottom - r.y});
  a.markUi("inspector", r);
  RectF body{r.x, headBottom + 1, r.w, r.bottom() - headBottom - 1};
  if (!tab) return;
  // Тело вкладки: прокрутка во всю ширину (колесо работает и у полей), содержимое — в колонке.
  ui::Area ar(body, 0);
  ui::Scroll sc(std::string("page.") + tab->id);
  ui::Indent in(col.x - body.x);
  ui::spacer(14);
  {
    ui::Group g(std::min(col.w, std::max(0.f, body.right() - col.x - 24)));
    ui::IdScope scope{int(sel.type)};
    ui::IdScope ts(tab->id);
    try {
      tab->draw(a, sel.id);
    } catch (const std::exception& e) {
      a.error(e);
    }
  }
  ui::spacer(24);
}

void directoryContent(App& a, const SectionDef& sec, RectF r) {
  RectF col = column(r, false, 720);
  RectF box{col.x, r.y + 16, col.w, r.h - 16};
  a.markUi("drawer", box);   // список каталога считает высоту по этому прямоугольнику
  ui::Area ar(box, 0);
  {
    ui::HStack hs(36, ui::Align::Left, 10);
    ui::icon(sec.icon, ui::Ink::Accent, 22);
    ui::label(sec.title, {.font = ui::Font::Heading});
  }
  ui::spacer(6);
  ui::Scroll sc(std::string("dir.") + sec.id);
  ui::IdScope s(sec.id);
  try {
    if (sec.directory) sec.directory(a);
  } catch (const std::exception& e) {
    a.error(e);
  }
}

}  // namespace

// ================================================================ заголовок и раздел страницы
const SectionDef* activeSection(App& a) {
  View v = a.view();
  if (v == View::Editor) {
    if (const EditorDef* e = findEditor(a.ui.editor); e && e->group) return findSection(e->group);
    v = beneathEditor(a);
  }
  if (v == View::Directory) return findSection(a.ui.directory);
  if (v == View::Entity) return a.sectionOf(a.ui.sel);
  return nullptr;
}

const char* pageTitle(App& a) {
  static std::string title;
  title.clear();
  switch (a.view()) {
    case View::Editor:
      if (const EditorDef* e = findEditor(a.ui.editor)) {
        if (e->group)
          if (const SectionDef* s = findSection(e->group)) title = std::string(s->title) + " · ";
        title += e->title;
      }
      break;
    case View::Directory:
      if (const SectionDef* s = findSection(a.ui.directory)) title = s->title;
      break;
    case View::Entity: {
      const SectionDef* s = a.sectionOf(a.ui.sel);
      title = s ? std::string(s->title) : std::string(selCaption(a.ui.sel.type));
      title += " · " + entityName(a.world(), a.ui.sel);
      break;
    }
    case View::Map: break;
  }
  return title.c_str();
}

// ================================================================ страница
void pageHost(App& a, RectF r) {
  const ui::Theme& th = ui::theme();
  const View v = a.view();
  RectF nav{r.x, r.y, kNavW, r.h};
  RectF content{r.x + kNavW, r.y, r.w - kNavW, r.h};

  const EditorDef* ed = v == View::Editor ? findEditor(a.ui.editor) : nullptr;
  const SectionDef* dir = v == View::Directory ? findSection(a.ui.directory) : nullptr;
  Selection sel = a.ui.sel;
  std::vector<const TabDef*> list;
  const TabDef* tab = nullptr;
  if (v == View::Entity) {
    list = visibleTabs(a, sel);
    tab = currentTab(a, sel, list);
  }

  // Навигация
  {
    ui::Panel p("pagenav", nav, docked(10));
    a.markUi("pagenav", nav);
    edgeLine(nav, false, false, true, false);
    ui::gap(2);
    backItem(a, v == View::Editor ? beneathEditor(a) != View::Map : v == View::Directory && a.ui.page && sel);
    ui::spacer(6);
    ui::Scroll sc("nav");   // низкое окно: пункты прокручиваются
    if (ed) navEditors(a, *ed);
    else if (dir) navDirectory(a, *dir);
    else if (v == View::Entity) navEntity(a, sel, list, tab);
  }
  // Содержимое
  {
    ui::Panel p("page", content, docked(0));
    ui::draw::rect(content, th.bg);
    a.markUi("page", content);
    if (ed) {
      editorContent(a, *ed, content);
    } else if (dir) {
      directoryContent(a, *dir, content);
    } else if (v == View::Entity) {
      if (const SectionDef* sec = a.sectionOf(sel)) a.ui.lastOf[sec->id] = sel;
      entityContent(a, sel, tab, content);
    }
  }
}

}  // namespace rg::app::detail::shell
