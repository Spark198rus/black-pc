// The in-game PC SETTINGS pages, built at start from the user's own front-end files (nothing of the game is in the
// port): copies of the game's own pages with the port's rows (fe/pc_settings.cpp, fe/bind_list.cpp).
//
// Main menu (FEMain.bin):
// - optionsmenuxbox (Options): a fifth row PC SETTINGS that opens page PCSettings. Its TextAnim (char 27) places the
//   rows transparent at depths 1/3/5/7 in frame 0; frames 46/49/52/55 make each opaque (Move + colour transform) and
//   drop a flash bar at depths 9-21. The new row: depth 2, its own reveal at frame 58 with a bar at depth 22.
//   PagePrepare / PageRelease / PageDestruct are renamed and wrapped: the wrappers add pcButton (BkMenuButton) to
//   the up/down ring.
// - pcsettings: a copy of Options with a fifth row (made like Options' PC SETTINGS row); the rows open the groups' pages, Back goes to Options instead of Options' own
//   save-and-leave.
// - a page per group and the KEY BINDINGS page: copies of Sound Setup (TextAnim char 49, six row slots revealed at
//   frames 43-58). A choice row is a clone of the PLAY MODE row (char 25: label 24 > 23, value 22 > 21, arrows 19)
//   whose value text shows the port's &PC_V<row> and whose BkListBox calls PCChange(row, -1 / +1). A slider row, a
//   link row and a key row are clones of the SFX row (char 40 > 39 > text 38): a slider adds the bullet bar (char 34,
//   26 frames) at 45 % on the value line and the number after it (BkMovieclipSlider from externObject.FEPCS<row>,
//   calling PCSlider<row>(0-100)); a key row adds its keys to the right.
// Pause menu (InGameOn.bin, PseMenu.bin): PC SETTINGS after OPTIONS opens one page whose rows the port fills
// (fe/pause_ui.cpp); see "pause menu" below.
// Captions are literal text: the game's text provider (FUN_001184D0) shows a text field's variable string as it is
// unless it starts with '$' (localized), '&' (a value) or '#'.
#include "fe/pages.h"

#include "common.h"
#include "fe/apt.h"
#include "fe/bind_list.h"
#include "fe/pause_ui.h"
#include "fe/pc_settings.h"
#include "fe/vars.h"
#include "language.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace {

using namespace apt;
using Push = std::function<void(Asm &)>;
using Path = std::vector<std::string>;

Push S(const std::string &s) { return [s](Asm &a) { a.push(Value::S(s)); }; }
Push I(int n) { return [n](Asm &a) { a.push(Value::I(n)); }; }
Push undefined() { return [](Asm &a) { a.push(Value::U()); }; }
Push nothing_fn() { return [](Asm &a) { a.function2("", [](Asm &) {}, 0x29); }; } // function () {}
Push at(const Path &p)
{
    return [p](Asm &a) {
        a.push(Value::S(p[0])).get_var();
        for (size_t i = 1; i < p.size(); i++) a.push(Value::S(p[i])).get_member();
    };
}

void call(Asm &a, const Path &obj, const std::string &method, const std::vector<Push> &args = {})
{
    for (auto it = args.rbegin(); it != args.rend(); ++it) (*it)(a);
    a.push(Value::I((int)args.size()));
    at(obj)(a);
    a.push(Value::S(method)).call_method().pop();
}

void call_function(Asm &a, const std::string &name) { a.push({Value::I(0), Value::S(name)}).call_function().pop(); }

void set(Asm &a, const Path &obj, const std::string &member, const Push &value)
{
    at(obj)(a);
    a.push(Value::S(member));
    value(a);
    a.set_member();
}

void make(Asm &a, const std::string &var, const std::string &cls, const std::vector<Push> &args = {})
{
    a.push(Value::S(var));
    for (auto it = args.rbegin(); it != args.rend(); ++it) (*it)(a);
    a.push({Value::I((int)args.size()), Value::S(cls)}).new_object().set_var();
}

// externObject's call fields, as the pages' own list objects fill them (the port's dispatcher takes "PC..." names)
void trigger(Asm &f, const std::string &name, const std::vector<Push> &params)
{
    const char *fields[3] = {"FEFunctionParam1", "FEFunctionParam2", "FEFunctionParam3"};
    set(f, {"_global", "externObject"}, "FEFunctionTrigger", I(1));
    set(f, {"_global", "externObject"}, "FEFunctionName", S(name));
    for (size_t i = 0; i < params.size() && i < 3; i++) set(f, {"_global", "externObject"}, fields[i], params[i]);
}

void literal(Movie &mv, uint32_t text, const std::string &s)
{
    if (rd(mv.apt, text) != 2) throw Error("not a text character");
    uint32_t p = mv.cstring(s);
    wr(mv.apt, text + 52, p);
    wr(mv.apt, text + 56, p);
}

Bytes body(const Movie &mv, uint32_t off) { return Bytes(mv.apt.begin() + off, mv.apt.begin() + off + 64); }

// A page defines its classes (BkMenuList, BkMenuButton and, by page, BkListBox, BkMovieclipSlider,
// BkMultiChoiceMenuButton, BkValueList, BkValuePoint) in DoInitActions only when _global has none yet, makes
// MyMenu = new BkMenuList in Prepare, and its Destruct deletes MyMenu, _global.BkMenuList, _global.BkMenuButton (and
// on some pages more of them). A DoInitAction runs only when the runtime loads the movie: a page entered again soon
// after it was left (still loaded) defined nothing, the page before had deleted the classes, so Prepare made no
// MyMenu (no focus, no Back: a dead page; the debug log showed the load seeing no _global.BkMenuList).
// Nothing of a page may stay in _global after its container goes, and a page must not release what another left
// there: a function keeps a reference to its container's movie entry, the game unloads containers (FEMain.bin at a
// mission start, PseMenu.bin at a resume when it needs the room) whatever is still referenced, and the entry released
// or run later walks freed or unrelocated data (crashes: FUN_000FC360 at the first pause; FUN_0010D8D0, frame table
// 0xA70, after the pause page's PcRow stayed in _global over a PseMenu reload). So on the pages the port changes:
// - first in frame 0, the page keeps what _global held for each of its classes (pcPrev<class>);
// - the class definitions run at every load (Movie::define_classes_on_load), and the page keeps them (pcOwn<class>);
//   the port's own classes (PcRow, KeyList) count as the page's;
// - Destruct runs the page's own Destruct0, then each class goes back to what the page found (pcPrev) where it is
//   still the page's, or stays as another page set it;
// - Update: a page (not destroyed) without MyMenu or its first button (row_var) puts its own classes back, makes MyMenu
//   again with what Prepare sets on it and runs PagePrepare again (a safety net).
using Classes = std::vector<std::string>;
std::string own_var(const std::string &cls) { return "pcOwn" + cls; }
std::string prev_var(const std::string &cls) { return "pcPrev" + cls; }
std::string keep_var(const std::string &cls) { return "pcKeep" + cls; }

bool fe_debug() { return getenv("BLACK_FE_DEBUG") != nullptr; }

Push defined(const Path &p) // true / false
{
    return [p](Asm &a) {
        at(p)(a);
        a.push(Value::U()).equals2().logical_not();
    };
}

// tests (BLACK_FE_DEBUG): queues an event in _global.pcE<n> (a short string: the call fields are 60-byte buffers the
// runtime fills without a bound, a longer string overruns them); dbg_flush sends one per update as PCLog
void dbg_log(Asm &a, const std::vector<Push> &parts)
{
    if (!fe_debug()) return;
    at({"_global"})(a);
    a.push(Value::S("pcE"));
    at({"_global", "pcEn"})(a);
    a.add2();
    a.push(Value::S(""));
    for (const Push &x : parts) {
        x(a);
        a.add2();
    }
    a.set_member();
    set(a, {"_global"}, "pcEn", [](Asm &g) { at({"_global", "pcEn"})(g); g.push(Value::I(1)).add2(); });
}

void dbg_flush(Asm &f)
{
    if (!fe_debug()) return;
    at({"_global", "pcEf"})(f);
    at({"_global", "pcEn"})(f);
    f.equals2();
    size_t empty = f.branch(0x9D);
    at({"_global", "externObject", "FEFunctionTrigger"})(f); // a call is waiting: next update
    f.push(Value::I(1)).equals2();
    size_t busy = f.branch(0x9D);
    trigger(f, "PCLog", {[](Asm &g) {
                at({"_global"})(g);
                g.push(Value::S("pcE"));
                at({"_global", "pcEf"})(g);
                g.add2().get_member();
            }});
    set(f, {"_global"}, "pcEf", [](Asm &g) { at({"_global", "pcEf"})(g); g.push(Value::I(1)).add2(); });
    f.land(empty);
    f.land(busy);
}

// more: the port's own classes the page defines in its script (before own_classes_script)
Classes own_classes_prepare(Movie &mv, const Classes &more = {})
{
    Classes classes = mv.define_classes_on_load();
    if (std::find(classes.begin(), classes.end(), "BkMenuList") == classes.end()) throw Error("a page without BkMenuList");
    classes.insert(classes.end(), more.begin(), more.end());
    mv.rename_function(mv.main_script(), "Destruct", "Destruct0");
    Asm a(mv); // before the definitions: what _global holds now
    for (const std::string &cls : classes) {
        a.push(Value::S(prev_var(cls)));
        at({"_global", cls})(a);
        a.set_var();
    }
    mv.prepend_action(a.finish());
    return classes;
}

void own_classes_script(Asm &a, const Classes &classes, const std::string &tag)
{
    if (fe_debug()) { // pcInst: this page instance's number
        at({"_global", "pcN"})(a);
        a.push(Value::U()).equals2().logical_not();
        size_t has = a.branch(0x9D);
        set(a, {"_global"}, "pcN", I(0));
        a.land(has);
        set(a, {"_global"}, "pcN", [](Asm &g) { at({"_global", "pcN"})(g); g.push(Value::I(1)).add2(); });
        a.push(Value::S("pcInst"));
        at({"_global", "pcN"})(a);
        a.set_var();
        at({"_global", "pcEn"})(a);
        a.push(Value::U()).equals2().logical_not();
        size_t log = a.branch(0x9D);
        set(a, {"_global"}, "pcEn", I(0));
        set(a, {"_global"}, "pcEf", I(0));
        a.land(log);
        dbg_log(a, {S("L" + tag + "#"), at({"pcInst"}), S(" g="), defined({"_global", "BkMenuList"}), S(" n=" + std::to_string(classes.size())),
                    S(" row="), defined({"_global", "PcRow"}), S(" key="), defined({"_global", "KeyList"}), S(" sl="), defined({"_global", "BkMovieclipSlider"})});
    }
    for (const std::string &cls : classes) { // at load: the classes this page just defined
        a.push(Value::S(own_var(cls)));
        at({"_global", cls})(a);
        a.set_var();
    }
    a.function2("Destruct", [classes, tag](Asm &f) {
        dbg_log(f, {S("D" + tag + "#"), at({"pcInst"}), S(" eq="), [](Asm &g) { at({"_global", "BkMenuList"})(g); at({own_var("BkMenuList")})(g); g.equals2(); }});
        f.push(Value::S("pcDead")).push_true().set_var();
        for (const std::string &cls : classes) { // after Destruct0: what the page found, or another page's class
            f.push(Value::S(keep_var(cls)));
            at({"_global", cls})(f);
            at({own_var(cls)})(f);
            f.equals2();
            size_t own = f.branch(0x9D);
            at({"_global", cls})(f);
            size_t done = f.branch(0x99);
            f.land(own);
            at({prev_var(cls)})(f);
            f.land(done);
            f.set_var();
        }
        call_function(f, "Destruct0");
        for (const std::string &cls : classes) {
            at({keep_var(cls)})(f);
            f.push(Value::U()).equals2();
            size_t none = f.branch(0x9D);
            set(f, {"_global"}, cls, at({keep_var(cls)}));
            size_t done = f.branch(0x99);
            f.land(none);
            at({"_global"})(f);
            f.push(Value::S(cls)).op(0x3A).pop(); // Delete
            f.land(done);
            for (const std::string &v : {keep_var(cls), prev_var(cls), own_var(cls)}) f.push(Value::S(v)).delete2().pop();
        }
    });
}

void emit_heal(Asm &f, const std::string &row_var, const Classes &classes, const std::string &tag)
{
    auto gone = [&f](const Path &p) {
        at(p)(f);
        f.push(Value::U()).equals2();
    };
    at({"pcDead"})(f);
    size_t fine0 = f.branch(0x9D);
    gone({"MyMenu"});
    size_t heal = f.branch(0x9D);
    gone({row_var});
    f.logical_not();
    size_t fine1 = f.branch(0x9D);
    f.land(heal);
    dbg_log(f, {S("H" + tag + "#"), at({"pcInst"}), S(" own="), defined({own_var("BkMenuList")}), S(" g="), defined({"_global", "BkMenuList"}),
                S(" menu="), defined({"MyMenu"})});
    for (const std::string &cls : classes) { // its own classes back
        gone({own_var(cls)});
        size_t none = f.branch(0x9D);
        set(f, {"_global"}, cls, at({own_var(cls)}));
        f.land(none);
    }
    gone({"_global", "BkMenuList"}); // nothing to make it from: leave it
    size_t fine2 = f.branch(0x9D);
    gone({"MyMenu"});
    f.logical_not();
    size_t has_menu = f.branch(0x9D);
    f.push(Value::S("MyMenu"));
    f.push({Value::I(0), Value::S("BkMenuList")}).new_object().set_var();
    const char *prompts[4][2] = {{"SetButtonSelector", "ButtonSelector"}, {"SetSelectPrompt", "SelectPrompt"},
                                 {"SetConfirmPrompt", "ConfirmPrompt"}, {"SetBackPrompt", "BackPrompt"}};
    for (auto &pr : prompts) call(f, {"MyMenu"}, pr[0], {at({"Main", pr[1]})});
    f.land(has_menu);
    call_function(f, "PagePrepare");
    trigger(f, "PCHealed", {});
    f.land(fine0);
    f.land(fine1);
    f.land(fine2);
}

void heal_update(Asm &a, const std::string &row_var, const Classes &classes, const std::string &tag)
{
    a.function2("Update", [row_var, classes, tag](Asm &f) {
        emit_heal(f, row_var, classes, tag);
        call_function(f, "Update0");
        dbg_flush(f);
    });
}

std::string lower(std::string s)
{
    for (char &c : s) c = (char)tolower((unsigned char)c);
    return s;
}

std::string ingame(const std::string &page) { return page + "IG"; }

// ---------------------------------------------------------------------------------------------------- rows
struct RowSpec {
    enum Kind { Choice, Slider, Link, Key } kind;
    int id;            // the setting (Choice, Slider) or the list row (Key)
    std::string label; // caption text: literal, or a variable for Key rows
    std::string value; // the value's variable ("" for Link)
    std::string page;  // Link: the page it opens
};

std::vector<RowSpec> group_rows(const pcset::Group &g, bool pause)
{
    std::vector<RowSpec> out;
    for (int n : g.rows) {
        if (pause && pcset::restart_only(n)) continue; // applies at the next start: main menu only
        std::string value = "&PC_V" + std::to_string(fevar::kValue + (int)out.size());
        if (const char *page = pcset::link(n)) out.push_back({RowSpec::Link, n, pcset::label(n), "", pause ? ingame(page) : page});
        else out.push_back({pcset::is_slider(n) ? RowSpec::Slider : RowSpec::Choice, n, pcset::label(n), value, ""});
    }
    return out;
}

std::vector<RowSpec> key_rows()
{
    std::vector<RowSpec> out;
    for (int i = 0; i < bindlist::kRows; i++)
        out.push_back({RowSpec::Key, i, "&PC_V" + std::to_string(fevar::kLabel + i), "&PC_V" + std::to_string(fevar::kValue + i), ""});
    return out;
}

// _global.<name>: a list object for rows, prototype.ProcessMapInput(code) and an empty Release. The menu calls
// ProcessMapInput on every update for the row that has the focus (FEInputId 0: no input).
void define_list_class(Asm &a, const std::string &name, const std::function<void(Asm &)> &input)
{
    at({"_global"})(a);
    a.push(Value::S(name));
    a.function2("", [](Asm &) {}, 0x29);
    a.set_member();
    for (const char *m : {"ProcessMapInput", "Release"}) {
        at({"_global", name, "prototype"})(a);
        a.push(Value::S(m));
        std::function<void(Asm &)> code = m[0] == 'P' ? input : std::function<void(Asm &)>([](Asm &) {});
        a.function2("", code, 0x129); // register 1 = this, 2 = _global
        a.set_member();
    }
}

void input_id(Asm &f) { f.push(Value::R(2)).push(Value::S("externObject")).get_member().push(Value::S("FEInputId")).get_member(); }

// KeyList (the KEY BINDINGS page): every input goes to the port as PCBind(mId, FEInputId, MyMenu.MovedCursor)
void define_key_list(Asm &a)
{
    define_list_class(a, "KeyList", [](Asm &f) {
        input_id(f);
        f.logical_not();
        size_t none = f.branch(0x9D);
        trigger(f, "PCBind", {[](Asm &g) { g.push(Value::R(1)).push(Value::S("mId")).get_member(); }, input_id,
                              at({"MyMenu", "MovedCursor"})});
        f.land(none);
    });
}

// PagePrepare / PageRelease / PageDestruct for a page of rows Main...<clip prefix><i> (bar: the slider's clip)
struct PageScript {
    std::vector<RowSpec> rows;
    std::function<Path(size_t)> clip, bar;
    std::string choice_class; // "BkListBox"
    std::string back;         // page Back goes to
    int page_id;              // PCPage(id) when prepared: the port fills the page's values (a group, or 100: keys)
    bool wrap;                // up on the first row goes to the last (not on the key list: it scrolls)
    std::vector<std::function<void(Asm &)>> extra_prepare;
};

void page_script(Movie &mv, const PageScript &p)
{
    uint32_t script = mv.main_script();
    for (const char *f : {"PagePrepare", "PageRelease", "PageDestruct"}) mv.rename_function(script, f, std::string(f) + "0");
    bool debug = getenv("BLACK_FE_DEBUG") != nullptr;
    mv.rename_function(script, "Update", "Update0");
    bool keys = std::any_of(p.rows.begin(), p.rows.end(), [](const RowSpec &r) { return r.kind == RowSpec::Key; });
    Classes classes = own_classes_prepare(mv, keys ? Classes{"KeyList"} : Classes{});
    Asm a(mv);
    if (keys) define_key_list(a);
    own_classes_script(a, classes, std::to_string(p.page_id));
    size_t k = p.rows.size();
    auto row_var = [](size_t i) { return "pcRow" + std::to_string(i); };
    auto list_var = [](size_t i) { return "pcList" + std::to_string(i); };
    for (size_t i = 0; i < k; i++) {
        a.push(Value::S(row_var(i))).define_local2();
        a.push(Value::S(list_var(i))).define_local2();
    }
    a.function2("PagePrepare", [&](Asm &f) {
        for (size_t i = 0; i < k; i++) {
            const RowSpec &r = p.rows[i];
            Path clip = p.clip(i);
            if (r.kind == RowSpec::Link) {
                make(f, row_var(i), "BkMenuButton", {at(clip)});
                set(f, {row_var(i)}, "actionSelectPage", S(r.page));
                continue;
            }
            make(f, row_var(i), "BkMultiChoiceMenuButton", {at(clip)});
            if (r.kind == RowSpec::Slider) {
                make(f, list_var(i), "BkMovieclipSlider",
                     {at(p.bar(i)), S("PCSlider" + std::to_string(r.id)), at({"_global", "externObject", "FEPCS" + std::to_string(r.id)})});
            } else if (r.kind == RowSpec::Key) {
                make(f, list_var(i), "KeyList");
                set(f, {list_var(i)}, "mId", I(r.id));
                set(f, {row_var(i)}, "actionSelectFunction", nothing_fn()); // select is not Back
            } else if (p.choice_class == "BkListBox") {
                Path left = clip, right = clip;
                left.insert(left.end(), {"Arrows", "LeftArrow"});
                right.insert(right.end(), {"Arrows", "RightArrow"});
                make(f, list_var(i), "BkListBox");
                call(f, {list_var(i)}, "SetId", {I(r.id)});
                call(f, {list_var(i)}, "SetGameFunction", {S("PCChange")});
                call(f, {list_var(i)}, "SetLeftAnimation", {at(left)});
                call(f, {list_var(i)}, "SetRightAnimation", {at(right)});
            } else {
                make(f, list_var(i), p.choice_class);
                set(f, {list_var(i)}, "mId", I(r.id));
            }
            set(f, {row_var(i)}, "mChoiceList", at({list_var(i)}));
        }
        for (size_t i = 0; i < k; i++) {
            if (p.wrap || i + 1 < k) set(f, {row_var(i)}, "buttonDown", at({row_var((i + 1) % k)}));
            if (p.wrap || i > 0) set(f, {row_var(i)}, "buttonUp", at({row_var((i + k - 1) % k)}));
        }
        call(f, {"MyMenu"}, "SetFocusButton", {at({row_var(0)})});
        call(f, {"MyMenu"}, "SetPageBack", {S(p.back)});
        if (debug) f.push(Value::S("pcMenu")).push(Value::S("MyMenu")).get_var().set_var();
        for (const auto &e : p.extra_prepare) e(f);
        trigger(f, "PCPage", {I(p.page_id)});
        f.push_true().ret();
    });
    a.function2("PageRelease", [&](Asm &f) {
        for (size_t i = 0; i < k; i++) {
            call(f, {row_var(i)}, "Release");
            if (p.rows[i].kind != RowSpec::Link) call(f, {list_var(i)}, "Release");
        }
    });
    a.function2("Update", [&](Asm &f) {
        emit_heal(f, row_var(0), classes, std::to_string(p.page_id));
        call_function(f, "Update0");
        if (debug) { // tests: each input reports the page's state (PCDebug in black.log)
            at({"_global", "externObject", "FEInputId"})(f);
            f.logical_not();
            size_t none = f.branch(0x9D);
            auto concat = [](std::vector<Push> parts) {
                return [parts](Asm &g) {
                    g.push(Value::S(""));
                    for (const Push &x : parts) {
                        x(g);
                        g.add2().push(Value::S(" ")).add2();
                    }
                };
            };
            trigger(f, "PCDebug", {concat({S("in"), at({"_global", "externObject", "FEInputId"}), S("rel"), at({"_global", "externObject", "FERelease"})}),
                                   concat({S("focus"), at({"MyMenu", "mFocusButton", "movieClip", "_name"}), S("frame"), at({"Main", "_currentframe"})}),
                                   concat({S("inst"), at({"pcInst"}), S("own"), defined({own_var("BkMenuList")}), S("g"), defined({"_global", "BkMenuList"}), S("back"), at({"MyMenu", "mstrPageBack"})})});
            f.land(none);
            dbg_flush(f);
        }
    });
    a.function2("PageDestruct", [&](Asm &f) {
        for (size_t i = 0; i < k; i++) {
            f.push(Value::S(row_var(i))).delete2().pop();
            f.push(Value::S(list_var(i))).delete2().pop();
        }
    });
    mv.append_control(mv.main, 0, mv.do_action(a.finish()));
}

// ---------------------------------------------------------------------------------------------------- main menu
// A row added below the bottom row of a menu's TextAnim. The game's menus (Options, the main menu) place each row's
// text (character 5 in sprite 6 in sprite 7 for the bottom one, at depth 1) transparent in frame 0 and reveal it at a
// frame (a Move with a colour transform) with a flash bar. The copy: depth 2, dy lower, named `name`, revealed 3
// frames after the bottom row with its own bar at new_bar_depth.
void add_menu_row(Movie &mv, uint32_t textanim, const std::string &label, const std::string &name, float dy, int reveal,
                  uint32_t bar_depth, uint32_t new_bar_depth)
{
    int t = mv.add_character(mv.clone_text(mv.character(5), label, label));
    int s6 = mv.add_character(mv.clone_sprite(mv.character(6), {{5, t}}));
    int s7 = mv.add_character(mv.clone_sprite(mv.character(7), {{6, s6}}));
    Bytes row = body(mv, mv.find_place(textanim, 0, 1));
    wr(row, 8, 2);
    wr(row, 12, (uint32_t)s7);
    wrf(row, 36, rdf(row, 36) + dy);
    wr(row, 52, mv.cstring(name));
    mv.append_control(textanim, 0, mv.alloc(row.data(), 64));
    Bytes show = body(mv, mv.find_place(textanim, reveal, 1, 1));
    wr(show, 8, 2);
    mv.append_control(textanim, reveal + 3, mv.alloc(show.data(), 64));
    Bytes bar = body(mv, mv.find_place(textanim, reveal, bar_depth));
    wr(bar, 8, new_bar_depth);
    wrf(bar, 36, rdf(bar, 36) + dy);
    mv.append_control(textanim, reveal + 3, mv.alloc(bar.data(), 64));
}

void options_row(Movie &mv)
{
    add_menu_row(mv, mv.character(27), "PC SETTINGS", "PCSettings_Text", 25.9f, 55, 21, 22); // below the Profile row

    uint32_t script = mv.main_script();
    for (const char *f : {"PagePrepare", "PageRelease", "PageDestruct", "Update"}) mv.rename_function(script, f, std::string(f) + "0");
    Classes classes = own_classes_prepare(mv);
    Asm a(mv);
    own_classes_script(a, classes, "opt");
    heal_update(a, "pcButton", classes, "opt");
    a.push(Value::S("pcButton")).define_local2();
    a.function2("PagePrepare", [](Asm &f) {
        call_function(f, "PagePrepare0");
        make(f, "pcButton", "BkMenuButton", {at({"Main", "TextAnim", "PCSettings_Text"})});
        set(f, {"Playerprofile"}, "buttonDown", at({"pcButton"}));
        set(f, {"pcButton"}, "buttonUp", at({"Playerprofile"}));
        set(f, {"pcButton"}, "buttonDown", at({"controllerButton"}));
        set(f, {"controllerButton"}, "buttonUp", at({"pcButton"}));
        set(f, {"pcButton"}, "actionSelectPage", S("PCSettings"));
        f.push_true().ret();
    });
    a.function2("PageRelease", [](Asm &f) {
        call_function(f, "PageRelease0");
        call(f, {"pcButton"}, "Release");
    });
    a.function2("PageDestruct", [](Asm &f) {
        call_function(f, "PageDestruct0");
        f.push(Value::S("pcButton")).delete2().pop();
    });
    mv.append_control(mv.main, 0, mv.do_action(a.finish()));
}

// The main menu (blacktitlemenu: CONTINUE MISSION / OPTIONS / OPERATOR PROFILE; blacktitlemenu2 with a new profile:
// START MISSION / OPTIONS) gets QUIT at the bottom, in the up / down ring between the last and the first button; it
// asks the port to close the game (PCQuit, like closing the window).
void main_menu_quit(Movie &mv, int textanim, float dy, int reveal, uint32_t bar_depth, uint32_t new_bar_depth,
                    const char *last, const char *first)
{
    add_menu_row(mv, mv.character(textanim), "QUIT", "Quit_Text", dy, reveal, bar_depth, new_bar_depth);
    uint32_t script = mv.main_script();
    for (const char *f : {"PagePrepare", "PageRelease", "PageDestruct", "Update"}) mv.rename_function(script, f, std::string(f) + "0");
    Classes classes = own_classes_prepare(mv);
    Asm a(mv);
    own_classes_script(a, classes, "main");
    heal_update(a, "quitButton", classes, "main");
    a.push(Value::S("quitButton")).define_local2();
    a.function2("PagePrepare", [last, first](Asm &f) {
        call_function(f, "PagePrepare0");
        make(f, "quitButton", "BkMenuButton", {at({"Main", "TextAnim", "Quit_Text"})});
        set(f, {last}, "buttonDown", at({"quitButton"}));
        set(f, {"quitButton"}, "buttonUp", at({last}));
        set(f, {"quitButton"}, "buttonDown", at({first}));
        set(f, {first}, "buttonUp", at({"quitButton"}));
        set(f, {"quitButton"}, "actionSelectFunction", [](Asm &g) {
            g.function2("", [](Asm &h) { trigger(h, "PCQuit", {}); });
        });
        f.push_true().ret();
    });
    a.function2("PageRelease", [](Asm &f) {
        call_function(f, "PageRelease0");
        call(f, {"quitButton"}, "Release");
    });
    a.function2("PageDestruct", [](Asm &f) {
        call_function(f, "PageDestruct0");
        f.push(Value::S("quitButton")).delete2().pop();
    });
    mv.append_control(mv.main, 0, mv.do_action(a.finish()));
}

void pc_menu(Movie &mv)
{
    const auto &groups = pcset::groups();
    if (groups.size() != 5) throw Error("the PC SETTINGS menu has five rows");
    literal(mv, mv.character(18), "PC SETTINGS");
    const int texts[4] = {14, 11, 8, 5}; // rows top to bottom: ControllerSetup, SoundSetup, PictureSetup, Playerprofile
    for (int i = 0; i < 4; i++) literal(mv, mv.character(texts[i]), groups[i].title);
    add_menu_row(mv, mv.character(27), groups[4].title, "PCGroup5_Text", 25.9f, 55, 21, 22); // the fifth, as in Options
    uint32_t script = mv.main_script();
    for (const char *f : {"PagePrepare", "PageRelease", "PageDestruct", "Update"}) mv.rename_function(script, f, std::string(f) + "0");
    Classes classes = own_classes_prepare(mv);
    Asm a(mv);
    own_classes_script(a, classes, "pcs");
    heal_update(a, "group5Button", classes, "pcs");
    a.push(Value::S("group5Button")).define_local2();
    a.function2("PagePrepare", [&groups](Asm &f) {
        call_function(f, "PagePrepare0");
        make(f, "group5Button", "BkMenuButton", {at({"Main", "TextAnim", "PCGroup5_Text"})});
        set(f, {"Playerprofile"}, "buttonDown", at({"group5Button"}));
        set(f, {"group5Button"}, "buttonUp", at({"Playerprofile"}));
        set(f, {"group5Button"}, "buttonDown", at({"controllerButton"}));
        set(f, {"controllerButton"}, "buttonUp", at({"group5Button"}));
        const char *buttons[5] = {"controllerButton", "soundButton", "pictureButton", "Playerprofile", "group5Button"};
        for (int i = 0; i < 5; i++) set(f, {buttons[i]}, "actionSelectPage", S(groups[i].page));
        call(f, {"MyMenu"}, "SetFunctionBack"); // no argument: not Options' own back (save and leave)
        call(f, {"MyMenu"}, "SetPageBack", {S("OptionsMenuXbox")});
        f.push_true().ret();
    });
    a.function2("PageRelease", [](Asm &f) {
        call_function(f, "PageRelease0");
        call(f, {"group5Button"}, "Release");
    });
    a.function2("PageDestruct", [](Asm &f) {
        call_function(f, "PageDestruct0");
        f.push(Value::S("group5Button")).delete2().pop();
    });
    mv.append_control(mv.main, 0, mv.do_action(a.finish()));
}

struct Slot {
    uint32_t depth;
    int reveal;
    uint32_t bar;
};
const Slot kSlots[] = {{227, 43, 229}, {123, 46, 231}, {121, 49, 233}, {17, 52, 235}, {9, 55, 237}, {1, 58, 239}};
constexpr float kRowY0 = -108.8f, kRowDY = 40.0f;
// the bullet bar at 45 % (a bullet is about 33 units tall) on the value line, centred about 16 below the row
constexpr float kBarScale = 0.45f, kBarDX = 12.0f, kBarDY = 16.0f - 15.9f * kBarScale, kNumberX = 140.0f;
constexpr float kKeysX = 175.0f; // a key row's keys, on its caption's line
constexpr uint32_t kBarDepth = 101;

void choice_page(Movie &mv, const std::string &title, const std::vector<RowSpec> &rows, const std::string &back, bool wrap, int page_id)
{
    if (rows.size() > 6) throw Error("a page has at most six rows");
    uint32_t textanim = mv.character(49);
    literal(mv, mv.character(42), title);
    Bytes proto = body(mv, mv.find_place(textanim, 0, 1)); // PlayModeButton: a choice row
    std::vector<uint32_t> frame0 = mv.controls(textanim, 0);
    for (size_t i = 0; i < 6; i++) {
        const Slot &s = kSlots[i];
        uint32_t old = mv.find_place(textanim, 0, s.depth);
        size_t at = std::find(frame0.begin(), frame0.end(), old) - frame0.begin(); // an index: frame0 grows below
        if (i >= rows.size()) { // unused slot: no row, no reveal, no bar
            frame0.erase(frame0.begin() + at);
            std::vector<uint32_t> keep;
            for (uint32_t c : mv.controls(textanim, s.reveal))
                if (rd(mv.apt, c) != 3 || (rd(mv.apt, c + 8) != s.depth && rd(mv.apt, c + 8) != s.bar)) keep.push_back(c);
            mv.set_controls(textanim, s.reveal, keep);
            continue;
        }
        const RowSpec &r = rows[i];
        float y = kRowY0 + kRowDY * i;
        int vs = -1;
        if (!r.value.empty()) {
            int vt = mv.add_character(mv.clone_text(mv.character(21), r.value, r.value));
            vs = mv.add_character(mv.clone_sprite(mv.character(22), {{21, vt}}));
        }
        int row;
        if (r.kind == RowSpec::Choice) {
            int lt = mv.add_character(mv.clone_text(mv.character(23), r.label, r.label));
            int ls = mv.add_character(mv.clone_sprite(mv.character(24), {{23, lt}}));
            row = mv.add_character(mv.clone_sprite(mv.character(25), {{24, ls}, {22, vs}}));
        } else {
            int lt = mv.add_character(mv.clone_text(mv.character(38), r.label, r.label));
            int ls = mv.add_character(mv.clone_sprite(mv.character(39), {{38, lt}}));
            uint32_t rc = mv.clone_sprite(mv.character(40), {{39, ls}});
            if (r.kind == RowSpec::Slider) mv.append_control(rc, 0, mv.place(3, vs, 0x06, kNumberX, 16.5f));
            if (r.kind == RowSpec::Key) mv.append_control(rc, 0, mv.place(3, vs, 0x06, kKeysX, -1.2f));
            row = mv.add_character(rc);
        }
        if (r.kind == RowSpec::Slider) {
            Bytes bar = body(mv, mv.find_place(textanim, 0, 123)); // Sound_Volume
            wr(bar, 8, kBarDepth + 2 * (uint32_t)i);
            wrf(bar, 16, kBarScale);
            wrf(bar, 20, 0);
            wrf(bar, 24, 0);
            wrf(bar, 28, kBarScale);
            wrf(bar, 32, rdf(proto, 32) + kBarDX);
            wrf(bar, 36, y + kBarDY);
            wr(bar, 52, mv.cstring("Slider" + std::to_string(i)));
            frame0.push_back(mv.alloc(bar.data(), 64));
            Bytes show = body(mv, mv.find_place(textanim, s.reveal, s.depth, 1));
            wr(show, 8, kBarDepth + 2 * (uint32_t)i);
            mv.append_control(textanim, s.reveal, mv.alloc(show.data(), 64));
        }
        Bytes b = proto;
        wr(b, 8, s.depth);
        wr(b, 12, (uint32_t)row);
        wrf(b, 36, y);
        wr(b, 52, mv.cstring("Row" + std::to_string(i)));
        frame0[at] = mv.alloc(b.data(), 64);
        wrf(mv.apt, mv.find_place(textanim, s.reveal, s.bar) + 36, y);
    }
    mv.set_controls(textanim, 0, frame0);

    PageScript p;
    p.rows = rows;
    p.clip = [](size_t i) { return Path{"Main", "TextAnim", "Row" + std::to_string(i)}; };
    p.bar = [](size_t i) { return Path{"Main", "TextAnim", "Slider" + std::to_string(i)}; };
    p.choice_class = "BkListBox";
    p.back = back;
    p.wrap = wrap;
    p.page_id = page_id;
    p.extra_prepare.push_back([](Asm &f) { // the Options background movie, as Sound Setup
        set(f, {"_global", "externObject"}, "FECurrentFmv", I(2));
        set(f, {"_global", "externObject"}, "FELoopFmv", I(1));
    });
    page_script(mv, p);
}

// ---------------------------------------------------------------------------------------------------- pause menu
// In play the front end runs InGameOn.bin (pausemenu: CONTINUE ... QUIT) and loads PseMenu.bin for the pause pages
// (pausemenuoptionxbox: the in-game Options). Their rows sit straight in a Main clip that slides in: frames 1-29
// move every row (Move + matrix) to its place, so a new row copies an existing row's moves. Rows are single lines:
// caption, then the value (or the bullet bar and the number) to the right.

// the moves of a depth (frames after 0)
std::vector<std::pair<int, Bytes>> moves_of(const Movie &mv, uint32_t sprite, uint32_t depth)
{
    std::vector<std::pair<int, Bytes>> out;
    for (int f = 1; f < (int)rd(mv.apt, sprite + 8); f++)
        for (uint32_t c : mv.controls(sprite, f))
            if (rd(mv.apt, c) == 3 && rd(mv.apt, c + 8) == depth && (rd(mv.apt, c + 4) & 1)) out.push_back({f, body(mv, c)});
    return out;
}

void add_moves(Movie &mv, uint32_t sprite, const std::vector<std::pair<int, Bytes>> &moves, uint32_t depth, float dy)
{
    for (const auto &[f, m] : moves) {
        Bytes b = m;
        wr(b, 8, depth);
        if (rd(b, 4) & 4) wrf(b, 36, rdf(b, 36) + dy);
        mv.append_control(sprite, f, mv.alloc(b.data(), 64));
    }
}

void shift_depth(Movie &mv, uint32_t sprite, uint32_t depth, float dy)
{
    for (int f = 0; f < (int)rd(mv.apt, sprite + 8); f++)
        for (uint32_t c : mv.controls(sprite, f))
            if (rd(mv.apt, c) == 3 && rd(mv.apt, c + 8) == depth && (rd(mv.apt, c + 4) & 4)) wrf(mv.apt, c + 36, rdf(mv.apt, c + 36) + dy);
}

void remove_depths(Movie &mv, uint32_t sprite, std::initializer_list<uint32_t> depths)
{
    for (int f = 0; f < (int)rd(mv.apt, sprite + 8); f++) {
        std::vector<uint32_t> keep, all = mv.controls(sprite, f);
        for (uint32_t c : all)
            if (rd(mv.apt, c) != 3 || std::find(depths.begin(), depths.end(), rd(mv.apt, c + 8)) == depths.end()) keep.push_back(c);
        if (keep.size() != all.size()) mv.set_controls(sprite, f, keep);
    }
}

void require_free(const Movie &mv, uint32_t sprite, uint32_t depth)
{
    for (int f = 0; f < (int)rd(mv.apt, sprite + 8); f++)
        for (uint32_t c : mv.controls(sprite, f))
            if (rd(mv.apt, c) == 3 && rd(mv.apt, c + 8) == depth) throw Error("depth " + std::to_string(depth) + " is in use");
}

// pausemenu: PC SETTINGS after OPTIONS (Main char 33; rows CONTINUE 89, OBJECTIVES STATUS 77, RESTART 87, OPTIONS 85
// and QUIT 83, 25 apart): the four rows above QUIT move up a row, the new one takes OPTIONS' place and moves
void pause_row(Movie &mv)
{
    uint32_t main_clip = mv.character(33);
    constexpr uint32_t kDepth = 97;
    require_free(mv, main_clip, kDepth);
    int lt = mv.add_character(mv.clone_text(mv.character(18), "PC SETTINGS", "PC SETTINGS"));
    int ls = mv.add_character(mv.clone_sprite(mv.character(19), {{18, lt}}));
    int row = mv.add_character(mv.clone_sprite(mv.character(20), {{19, ls}}));
    Bytes b = body(mv, mv.find_place(main_clip, 0, 85));
    wr(b, 8, kDepth);
    wr(b, 12, (uint32_t)row);
    wr(b, 52, mv.cstring("PCSettings"));
    auto moves = moves_of(mv, main_clip, 85);
    mv.append_control(main_clip, 0, mv.alloc(b.data(), 64));
    add_moves(mv, main_clip, moves, kDepth, 0);
    for (uint32_t d : {89u, 77u, 87u, 85u}) shift_depth(mv, main_clip, d, -25.0f);

    uint32_t script = mv.main_script();
    for (const char *f : {"PagePrepare", "PageRelease", "PageDestruct", "Update"}) mv.rename_function(script, f, std::string(f) + "0");
    Classes classes = own_classes_prepare(mv);
    Asm a(mv);
    own_classes_script(a, classes, "pause");
    heal_update(a, "pcButton", classes, "pause");
    a.push(Value::S("pcButton")).define_local2();
    a.function2("PagePrepare", [](Asm &f) {
        call_function(f, "PagePrepare0");
        make(f, "pcButton", "BkMenuButton", {at({"Main", "PCSettings"})});
        set(f, {"optionsButton"}, "buttonDown", at({"pcButton"}));
        set(f, {"pcButton"}, "buttonUp", at({"optionsButton"}));
        set(f, {"pcButton"}, "buttonDown", at({"quitButton"}));
        set(f, {"quitButton"}, "buttonUp", at({"pcButton"}));
        set(f, {"pcButton"}, "actionSelectPage", S(ingame("PCPage")));
        f.push_true().ret();
    });
    a.function2("PageRelease", [](Asm &f) {
        call_function(f, "PageRelease0");
        call(f, {"pcButton"}, "Release");
    });
    a.function2("PageDestruct", [](Asm &f) {
        call_function(f, "PageDestruct0");
        f.push(Value::S("pcButton")).delete2().pop();
    });
    mv.append_control(mv.main, 0, mv.do_action(a.finish()));
}

// the PC SETTINGS pause page (fe/pause_ui.cpp): one copy of pausemenuoptionxbox (Main char 50; the Vibration row 79
// at y -33.8 is the pattern) whose six rows show what the port puts in their values. Its own rows (toggles 75-91
// with ON/OFF texts, sliders 93/199 and their captions 197/303) go.
constexpr float kIgRowY0 = -87.9f, kIgRowDY = 27.0f, kIgPatternY = -33.8f;
constexpr float kIgValueX = 205.0f, kIgBarX = 185.0f, kIgBarScale = 0.5f, kIgNumberX = 313.0f;
constexpr float kIgBarY = -15.3f * kIgBarScale; // the bar's middle is 15.3 below its origin, the text's at the origin

std::string var(int n) { return "&PC_V" + std::to_string(n); }

// row i's packed state externObject.FEPCR<i>, shifted right and masked: pushes (R >> shift) & mask
Push row_bits(size_t i, int shift, int mask)
{
    return [i, shift, mask](Asm &f) {
        at({"_global", "externObject", "FEPCR" + std::to_string(i)})(f);
        if (shift) f.push(Value::I(shift)).op(0x64); // BitRShift
        f.push(Value::I(mask)).op(0x60);             // BitAnd
    };
}

// pcRows[((R >> shift) & 15) - 1]: the row a link points at (index -1: undefined, no row)
Push row_link(size_t i, int shift)
{
    return [i, shift](Asm &f) {
        at({"pcRows"})(f);
        row_bits(i, shift, 15)(f);
        f.push(Value::I(1)).op(0x0B); // Subtract
        f.get_member();
    };
}

void pause_page(Movie &mv)
{
    using pauseui::kRows;
    uint32_t main_clip = mv.character(50);
    literal(mv, mv.character(47), var(fevar::kTitle));
    Bytes proto = body(mv, mv.find_place(main_clip, 0, 79));
    auto moves = moves_of(mv, main_clip, 79);
    remove_depths(mv, main_clip, {75, 77, 79, 81, 83, 85, 87, 89, 91, 93, 197, 199, 303});
    for (int i = 0; i < kRows; i++) {
        float y = kIgRowY0 + kIgRowDY * i;
        uint32_t depth = 75 + 2 * (uint32_t)i;
        std::string label = var(fevar::kLabel + i);
        int lt = mv.add_character(mv.clone_text(mv.character(18), label, label));
        int ls = mv.add_character(mv.clone_sprite(mv.character(19), {{18, lt}}));
        uint32_t rc = mv.clone_sprite(mv.character(20), {{19, ls}});
        uint32_t text_depth = 3;
        for (auto [n, x] : {std::pair<int, float>{fevar::kValue + i, kIgValueX}, {fevar::kNumber + i, kIgNumberX}}) {
            int vt = mv.add_character(mv.clone_text(mv.character(15), var(n), var(n)));
            int vs = mv.add_character(mv.clone_sprite(mv.character(16), {{15, vt}}));
            mv.append_control(rc, 0, mv.place(text_depth, vs, 0x06, x, 0.1f));
            text_depth += 2;
        }
        mv.append_control(rc, 0, mv.place(text_depth, 39, 0x06, kIgBarX, kIgBarY, kIgBarScale, "Bar")); // the bullet bar
        int row = mv.add_character(rc);
        Bytes b = proto;
        wr(b, 8, depth);
        wr(b, 12, (uint32_t)row);
        wrf(b, 36, y);
        wr(b, 52, mv.cstring("Row" + std::to_string(i)));
        mv.append_control(main_clip, 0, mv.alloc(b.data(), 64));
        add_moves(mv, main_clip, moves, depth, y - kIgPatternY);
    }

    uint32_t script = mv.main_script();
    for (const char *f : {"PagePrepare", "PageRelease", "PageDestruct", "Update"}) mv.rename_function(script, f, std::string(f) + "0");
    Classes classes = own_classes_prepare(mv, {"PcRow"});
    Asm a(mv);
    // PcRow: inputs 1-5 (not 0: none, not 6: Back, which reports itself) go to the port as PCRow(mId, input, moved)
    define_list_class(a, "PcRow", [](Asm &f) {
        input_id(f);
        f.push(Value::I(5)).op(0x67); // Greater
        size_t skip1 = f.branch(0x9D);
        input_id(f);
        f.logical_not();
        size_t skip2 = f.branch(0x9D);
        trigger(f, "PCRow", {[](Asm &g) { g.push(Value::R(1)).push(Value::S("mId")).get_member(); }, input_id,
                             at({"MyMenu", "MovedCursor"})});
        f.land(skip1);
        f.land(skip2);
    });
    own_classes_script(a, classes, "pp");
    auto row_var = [](size_t i) { return "pcRow" + std::to_string(i); };
    auto list_var = [](size_t i) { return "pcList" + std::to_string(i); };
    auto clip = [](size_t i) { return Path{"Main", "Row" + std::to_string(i)}; };
    for (size_t i = 0; i < kRows; i++) {
        a.push(Value::S(row_var(i))).define_local2();
        a.push(Value::S(list_var(i))).define_local2();
    }
    a.push(Value::S("pcRows")).define_local2();
    a.push(Value::S("pcLayout")).define_local2();
    a.function2("PagePrepare", [&](Asm &f) {
        for (size_t i = 0; i < kRows; i++) {
            make(f, row_var(i), "BkMultiChoiceMenuButton", {at(clip(i))});
            make(f, list_var(i), "PcRow");
            set(f, {list_var(i)}, "mId", I((int)i));
            set(f, {row_var(i)}, "mChoiceList", at({list_var(i)}));
            set(f, {row_var(i)}, "actionSelectFunction", nothing_fn()); // select is not Back
        }
        f.push(Value::S("pcRows"));
        for (size_t i = kRows; i-- > 0;) at({row_var(i)})(f);
        f.push(Value::I(kRows)).op(0x42).set_var(); // InitArray
        f.push({Value::S("pcLayout"), Value::I(-1)}).set_var();
        call(f, {"MyMenu"}, "SetFunctionBack", {[](Asm &g) {
                 g.function2("", [](Asm &h) { // at the top screen Back leaves for the pause menu, below it goes up
                     at({"_global", "externObject", "FEPCAtRoot"})(h);
                     h.logical_not();
                     size_t below = h.branch(0x9D);
                     set(h, {"_global", "externObject"}, "FETargetPage", S("PauseMenu"));
                     set(h, {"_global", "externObject"}, "FEChangePage", I(1));
                     size_t done = h.branch(0x99);
                     h.land(below);
                     trigger(h, "PCBack", {});
                     h.land(done);
                 });
             }});
        call(f, {"MyMenu"}, "SetFocusButton", {at({row_var(0)})});
        trigger(f, "PCOpen", {at({"Main", "Row0", "Bar", "_totalframes"})});
        f.push_true().ret();
    });
    a.function2("Update", [&](Asm &f) {
        emit_heal(f, row_var(0), classes, "pp");
        call_function(f, "Update0");
        dbg_flush(f);
        at({"_global", "externObject", "FEPCLayout"})(f); // a new screen: rows shown, linked, focused
        at({"pcLayout"})(f);
        f.equals2();
        size_t same = f.branch(0x9D);
        f.push(Value::S("pcLayout"));
        at({"_global", "externObject", "FEPCLayout"})(f);
        f.set_var();
        for (size_t i = 0; i < kRows; i++) {
            set(f, clip(i), "_visible", row_bits(i, 0, 1));
            set(f, {row_var(i)}, "buttonDown", row_link(i, 16));
            set(f, {row_var(i)}, "buttonUp", row_link(i, 20));
        }
        call(f, {"MyMenu"}, "SetFocusButton", {[](Asm &g) {
                 at({"pcRows"})(g);
                 at({"_global", "externObject", "FEPCFocus"})(g);
                 g.get_member();
             }});
        f.land(same);
        for (size_t i = 0; i < kRows; i++) { // the bars, every update
            Path bar = clip(i);
            bar.push_back("Bar");
            set(f, bar, "_visible", row_bits(i, 1, 1));
            call(f, bar, "gotoAndStop", {row_bits(i, 8, 255)});
        }
    });
    a.function2("PageRelease", [&](Asm &f) {
        for (size_t i = 0; i < kRows; i++) {
            call(f, {row_var(i)}, "Release");
            call(f, {list_var(i)}, "Release");
        }
    });
    a.function2("PageDestruct", [&](Asm &f) {
        for (size_t i = 0; i < kRows; i++) {
            f.push(Value::S(row_var(i))).delete2().pop();
            f.push(Value::S(list_var(i))).delete2().pop();
        }
        f.push(Value::S("pcRows")).delete2().pop();
    });
    mv.append_control(mv.main, 0, mv.do_action(a.finish()));
}

// ---------------------------------------------------------------------------------------------------- files
bool read_file(const std::wstring &path, Bytes &out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

bool write_file(const std::wstring &path, const Bytes &data)
{
    for (size_t i = path.find(L'\\', 3); i != std::wstring::npos; i = path.find(L'\\', i + 1))
        CreateDirectoryW(path.substr(0, i).c_str(), nullptr); // each folder on the way
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write((const char *)data.data(), (std::streamsize)data.size());
    f.close();
    if (!f) logf("fe: cannot write %s", narrow(path).c_str());
    return (bool)f;
}

// one container: read it, let edit() change it, keep the result; false (logged) if it does not fit
bool rebuild(const std::wstring &game_dir, const wchar_t *file, const std::function<void(Container &)> &edit, Bytes &out)
{
    std::wstring path = game_dir + L"\\export\\frontend\\" + file;
    Bytes in;
    if (!read_file(path, in)) {
        logf("fe: %s not found", narrow(path).c_str());
        return false;
    }
    try {
        Container c;
        c.parse(in);
        edit(c);
        out = c.build();
        return true;
    } catch (const std::exception &e) {
        logf("fe: %s is not laid out as expected (%s)", narrow(path).c_str(), e.what());
        return false;
    }
}

// The port runs on a PC: the game's messages (saving and loading, free blocks, the hard disk check) must not
// send the player to a console. Each language gets the wording it needs; the local word for the PC replaces
// the console's name, with the surrounding phrasing adjusted. The patterns are words, not game text.
struct Replacement {
    const char *from, *to;
};

std::vector<Replacement> text_replacements()
{
    static const Replacement kEnglish[] = {{"XBOX CONSOLE", "COMPUTER"}, {"XBOX", "COMPUTER"}};
    static const Replacement kSpanish[] = {{"LA CONSOLA XBOX", "EL PC"}, {"MEMORIA DE XBOX", "MEMORIA DEL PC"},
                                           {"XBOX NO", "EL PC NO"}, {"XBOX", "PC"}};
    static const Replacement kGerman[] = {{"DIE XBOX-KONSOLE", "DEN PC"}, {"XBOX-FESTPLATTE", "PC-FESTPLATTE"},
                                          {"DIE XBOX HAT", "DER PC HAT"}, {"XBOX-STEUERUNG", "PC-STEUERUNG"},
                                          {"XBOX", "PC"}};
    static const Replacement kFrench[] = {{"la console Xbox", "le PC"}, {"disque dur Xbox", "disque dur du PC"},
                                          {"La Xbox", "Le PC"}, {"Xbox", "PC"}};
    static const Replacement kItalian[] = {{"spegnere Xbox", "spegnere il PC"}, {"di Xbox", "del PC"},
                                           {"Xbox non", "Il PC non"}, {"Xbox", "PC"}};
    const std::string &language = language_name();
    if (language == "es") return {std::begin(kSpanish), std::end(kSpanish)};
    if (language == "de") return {std::begin(kGerman), std::end(kGerman)};
    if (language == "fr") return {std::begin(kFrench), std::end(kFrench)};
    if (language == "it") return {std::begin(kItalian), std::end(kItalian)};
    return {std::begin(kEnglish), std::end(kEnglish)};
}

// The game's text (language\strings\MainXX.bin: a magic word, a word, the count n, the table offset 16; n offsets,
// ascending; each entry a 4-byte key (entries sorted by key) and a NUL-terminated string, padded to 4 bytes except the
// last): the file the resolved language makes the game read, patched so its messages name a PC. Keys and order stay;
// only the offsets move.
bool patch_strings(const std::wstring &game_dir, const std::wstring &out_root)
{
    const std::wstring rel = L"\\language\\strings\\" + language_strings_file();
    Bytes in;
    if (!read_file(game_dir + rel, in)) {
        logf("fe: %s not found", narrow(game_dir + rel).c_str());
        return false;
    }
    try {
        if (in.size() < 16) throw Error("too short");
        uint32_t n = rd(in, 8), table = rd(in, 12);
        if (table != 16 || table + 4ull * n > in.size()) throw Error("no string table");
        Bytes out(in.begin(), in.begin() + table + 4 * n);
        int changed = 0;
        for (uint32_t i = 0; i < n; i++) {
            uint32_t at = rd(in, table + 4 * i), end = i + 1 < n ? rd(in, table + 4 * (i + 1)) : (uint32_t)in.size();
            if (at + 4 > end || end > in.size()) throw Error("an entry out of order");
            std::string s((const char *)&in[at + 4], strnlen((const char *)&in[at + 4], end - at - 4)), t = s;
            for (const Replacement &r : text_replacements())
                for (size_t p = t.find(r.from); p != std::string::npos; p = t.find(r.from, p + strlen(r.to)))
                    t.replace(p, strlen(r.from), r.to);
            changed += t != s;
            wr(out, table + 4 * i, (uint32_t)out.size());
            out.insert(out.end(), in.begin() + at, in.begin() + at + 4);
            out.insert(out.end(), t.begin(), t.end());
            out.push_back(0);
            while (i + 1 < n && out.size() % 4) out.push_back(0);
        }
        if (!write_file(out_root + rel, out)) return false;
        logf("fe: game text (%s): %d strings name the PC instead of the Xbox", language_name().c_str(), changed);
        return true;
    } catch (const std::exception &e) {
        logf("fe: %s is not laid out as expected (%s)", narrow(game_dir + rel).c_str(), e.what());
        DeleteFileW((out_root + rel).c_str());
        return false;
    }
}

const Bytes &movie(const Container &c, const char *name)
{
    auto it = c.movies.find(name);
    if (it == c.movies.end()) throw Error(std::string(name) + " missing");
    return it->second;
}

void add_movie(Container &c, const std::string &name, Bytes data)
{
    c.movies[name] = std::move(data);
    c.order.push_back(name);
}

constexpr char kBindings[] = "PCBindings";

} // namespace

bool fe_build_pages(const std::wstring &game_dir, const std::wstring &out_root)
{
    const std::wstring dir = out_root + L"\\export\\frontend\\";
    // the main menu: Options gets PC SETTINGS, which opens a copy of Options, whose rows open copies of Sound Setup
    Bytes fe;
    bool main_ok = rebuild(game_dir, L"FEMain.bin", [](Container &c) {
        const Bytes options = movie(c, "optionsmenuxbox"), sound = movie(c, "soundsetupxbox");
        Movie menu(options);
        pc_menu(menu);
        add_movie(c, "pcsettings", menu.build());
        std::string controls;
        int index = 0;
        for (const pcset::Group &g : pcset::groups()) {
            Movie page(sound);
            choice_page(page, g.title, group_rows(g, false), "PCSettings", true, index++);
            add_movie(c, lower(g.page), page.build());
            for (int n : g.rows)
                if (pcset::link(n)) controls = g.page;
        }
        Movie keys(sound);
        choice_page(keys, "KEY BINDINGS", key_rows(), controls, false, 100);
        add_movie(c, lower(kBindings), keys.build());
        Movie opt(options);
        options_row(opt);
        c.movies["optionsmenuxbox"] = opt.build();
        Movie title(movie(c, "blacktitlemenu")); // QUIT under OPERATOR PROFILE (rows 25.1 apart, revealed 43/46/49)
        main_menu_quit(title, 18, 25.1f, 49, 11, 12, "profileButton", "continueButton");
        c.movies["blacktitlemenu"] = title.build();
        Movie fresh(movie(c, "blacktitlemenu2")); // QUIT under OPTIONS (revealed 43/46)
        main_menu_quit(fresh, 14, 25.0f, 46, 7, 8, "optionsButton", "startButton");
        c.movies["blacktitlemenu2"] = fresh.build();
    }, fe) && write_file(dir + L"FEMain.bin", fe);
    if (!main_ok) DeleteFileW((dir + L"FEMain.bin").c_str()); // not an old build of the pages
    // the pause menu: PC SETTINGS after OPTIONS (InGameOn.bin) opens the one PC SETTINGS pause page (a copy of the
    // in-game Options in PseMenu.bin, loaded with the pause pages); both files or neither
    Bytes on, pse;
    bool pause_ok = rebuild(game_dir, L"InGameOn.bin", [](Container &c) {
        Movie root(movie(c, "pausemenu"));
        pause_row(root);
        c.movies["pausemenu"] = root.build();
    }, on) && rebuild(game_dir, L"PseMenu.bin", [](Container &c) {
        Movie page(movie(c, "pausemenuoptionxbox"));
        pause_page(page);
        add_movie(c, lower(ingame("PCPage")), page.build());
    }, pse) && write_file(dir + L"InGameOn.bin", on) && write_file(dir + L"PseMenu.bin", pse);
    if (!pause_ok) { // a half-written pair would point the pause menu at pages that are not there
        DeleteFileW((dir + L"InGameOn.bin").c_str());
        DeleteFileW((dir + L"PseMenu.bin").c_str());
    }
    logf("fe: PC SETTINGS pages: main menu %s, pause menu %s (%s)", main_ok ? "on" : "off", pause_ok ? "on" : "off",
         narrow(out_root).c_str());
    bool text_ok = patch_strings(game_dir, out_root);
    return main_ok || pause_ok || text_ok;
}
