// Editing BLACK's front-end containers (see apt.h for the structures).
//
// Container: magic 40 79 30 26 37 E5 92 95, entry count +8, entry table offset (0x80) +12; each entry
// {name offset, region offset}. A region: 0x80-byte header (+4/+8 texture count / region-relative texture offsets,
// +0x0C apt data offset (0x80), +0x10 constant block, +0x14 texture table, +0x18 geometry block), the apt data, the
// constant block ("Apt constant file": +0x14 main character, +0x18/+0x1C constant count / table of {type, value}),
// then geometry and textures (0x80-aligned).
//
// Loading a page relocates its movie in place and unloading puts the offsets back (FUN_001099F0, FUN_000FA100,
// FUN_000F6F00). Unloading also rewrites every Push / DefineDictionary item from a running counter, in the order it
// walks the movie: characters in table order (0 = the main timeline), a sprite's frames and controls, a PlaceObject's
// clip actions, each stream front to back with function bodies inline. The files number their items 0..n-1 in that
// order, one constant each, so build() renumbers every item by its place in the walk and writes the constant table
// in that order. No structure may be shared by two pointers (each is relocated once per pointer).
#include "fe/apt.h"

#include <algorithm>
#include <cstring>

namespace apt {
namespace {

const uint8_t kMagic[8] = {0x40, 0x79, 0x30, 0x26, 0x37, 0xE5, 0x92, 0x95};
constexpr uint32_t kSig = 0x09876543;

void pad(Bytes &b, size_t n)
{
    while (b.size() % n) b.push_back(0);
}

uint32_t align4(uint32_t p) { return (p + 3) & ~3u; }

// the opcode after the one at pos (operand sizes as the Apt VM reads them; operands are 4-aligned)
uint32_t skip_op(const Bytes &b, uint32_t pos)
{
    if (pos >= b.size()) throw Error("ActionScript runs past the movie");
    uint8_t op = b[pos];
    uint32_t p = pos + 1;
    switch (op) {
    case 119: case 180: case 183: return p + 4;
    case 129: case 135: case 153: case 157: case 159: case 184: return align4(p) + 4;
    case 181: case 185: case 162: case 174: case 175: case 176: case 177: case 178: case 179: return p + 1;
    case 182: case 163: return p + 2;
    case 131: case 136: case 150: return align4(p) + 8;
    case 139: case 140: case 161: case 164: case 165: case 166: case 167: case 148: return align4(p) + 4;
    case 142: return align4(p) + 28;
    case 155: return align4(p) + 24;
    case 143: return align4(p) + 16;
    default: return p;
    }
}

} // namespace

uint32_t rd(const Bytes &b, size_t off)
{
    if (off + 4 > b.size()) throw Error("read past the end");
    uint32_t v;
    memcpy(&v, b.data() + off, 4);
    return v;
}

void wr(Bytes &b, size_t off, uint32_t v)
{
    if (off + 4 > b.size()) throw Error("write past the end");
    memcpy(b.data() + off, &v, 4);
}

float rdf(const Bytes &b, size_t off)
{
    uint32_t v = rd(b, off);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

void wrf(Bytes &b, size_t off, float f)
{
    uint32_t v;
    memcpy(&v, &f, 4);
    wr(b, off, v);
}

Value Value::F(float f)
{
    uint32_t v;
    memcpy(&v, &f, 4);
    return {6, v, {}};
}

// ---------------------------------------------------------------------------------------------------- Container
void Container::parse(const Bytes &data)
{
    if (data.size() < 0x80 || memcmp(data.data(), kMagic, 8) != 0) throw Error("not a front-end container");
    uint32_t count = rd(data, 8), base = rd(data, 12);
    std::vector<std::pair<uint32_t, std::string>> regions;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t e = rd(data, base + 4 * i), name = rd(data, e), region = rd(data, e + 4);
        std::string n;
        for (uint32_t p = name; p < data.size() && data[p]; p++) n += (char)data[p];
        order.push_back(n);
        regions.push_back({region, n});
    }
    std::sort(regions.begin(), regions.end());
    for (size_t i = 0; i < regions.size(); i++) {
        size_t end = i + 1 < regions.size() ? regions[i + 1].first : data.size();
        if (regions[i].first > end || end > data.size()) throw Error("bad container entry");
        movies[regions[i].second] = Bytes(data.begin() + regions[i].first, data.begin() + end);
    }
}

Bytes Container::build() const
{
    Bytes out(kMagic, kMagic + 8);
    out.resize(0x80);
    wr(out, 8, (uint32_t)order.size());
    wr(out, 12, 0x80);
    size_t table = out.size();
    out.resize(out.size() + 4 * order.size());
    size_t ent = out.size();
    out.resize(out.size() + 8 * order.size());
    std::vector<uint32_t> names;
    for (const std::string &n : order) {
        names.push_back((uint32_t)out.size());
        out.insert(out.end(), n.begin(), n.end());
        out.push_back(0);
    }
    pad(out, 0x80);
    for (size_t i = 0; i < order.size(); i++) {
        wr(out, table + 4 * i, (uint32_t)(ent + 8 * i));
        wr(out, ent + 8 * i, names[i]);
        wr(out, ent + 8 * i + 4, (uint32_t)out.size());
        const Bytes &m = movies.at(order[i]);
        out.insert(out.end(), m.begin(), m.end());
        pad(out, 0x80);
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------- Movie
Movie::Movie(const Bytes &r)
{
    if (r.size() < 0x80 || rd(r, 0x0C) != 0x80) throw Error("not an Apt movie region");
    uint32_t c = rd(r, 0x10), g = rd(r, 0x18);
    if (c < 0x80 || c > r.size() || (g && (g < c || g > r.size()))) throw Error("bad movie header");
    hdr_.assign(r.begin(), r.begin() + 0x80);
    apt.assign(r.begin() + 0x80, r.begin() + c);
    cst_.assign(r.begin() + c, g ? r.begin() + g : r.end());
    if (g) tail_.assign(r.begin() + g, r.end());
    main = rd(cst_, 0x14);
    uint32_t n = rd(cst_, 0x18), a = rd(cst_, 0x1C);
    for (uint32_t i = 0; i < n; i++) consts_.push_back({rd(cst_, a + 8 * i), rd(cst_, a + 8 * i + 4)});
}

uint32_t Movie::alloc(const void *data, size_t size, size_t align)
{
    pad(apt, align);
    uint32_t off = (uint32_t)apt.size();
    apt.insert(apt.end(), (const uint8_t *)data, (const uint8_t *)data + size);
    return off;
}

uint32_t Movie::alloc_words(const std::vector<uint32_t> &words) { return alloc(words.data(), 4 * words.size()); }

uint32_t Movie::cstring(const std::string &s)
{
    auto it = strings_.find(s);
    if (it != strings_.end()) return it->second;
    uint32_t off = alloc(s.c_str(), s.size() + 1, 1);
    strings_[s] = off;
    return off;
}

std::string Movie::str(uint32_t off) const
{
    std::string s;
    for (uint32_t p = off; off && p < apt.size() && apt[p]; p++) s += (char)apt[p];
    return s;
}

std::vector<uint32_t> Movie::characters() const
{
    uint32_t n = rd(apt, main + 20), a = rd(apt, main + 24);
    std::vector<uint32_t> out;
    for (uint32_t i = 0; i < n; i++) out.push_back(rd(apt, a + 4 * i));
    return out;
}

uint32_t Movie::character(int id) const
{
    std::vector<uint32_t> c = characters();
    if (id < 0 || id >= (int)c.size() || !c[id] || rd(apt, c[id] + 4) != kSig) throw Error("missing character " + std::to_string(id));
    return c[id];
}

int Movie::add_character(uint32_t off)
{
    std::vector<uint32_t> c = characters();
    c.push_back(off);
    uint32_t arr = alloc_words(c);
    wr(apt, main + 20, (uint32_t)c.size());
    wr(apt, main + 24, arr);
    return (int)c.size() - 1;
}

std::vector<uint32_t> Movie::controls(uint32_t sprite, int frame) const
{
    if (frame < 0 || (uint32_t)frame >= rd(apt, sprite + 8)) throw Error("no frame " + std::to_string(frame));
    uint32_t f = rd(apt, sprite + 12) + 8 * frame, n = rd(apt, f), a = rd(apt, f + 4);
    std::vector<uint32_t> out;
    for (uint32_t i = 0; i < n; i++) out.push_back(rd(apt, a + 4 * i));
    return out;
}

void Movie::set_controls(uint32_t sprite, int frame, const std::vector<uint32_t> &ctls)
{
    controls(sprite, frame); // range check
    uint32_t f = rd(apt, sprite + 12) + 8 * frame;
    uint32_t arr = ctls.empty() ? 0 : alloc_words(ctls);
    wr(apt, f, (uint32_t)ctls.size());
    wr(apt, f + 4, arr);
}

void Movie::append_control(uint32_t sprite, int frame, uint32_t ctl)
{
    std::vector<uint32_t> c = controls(sprite, frame);
    c.push_back(ctl);
    set_controls(sprite, frame, c);
}

uint32_t Movie::find_place(uint32_t sprite, int frame, uint32_t depth, int move) const
{
    for (uint32_t c : controls(sprite, frame))
        if (rd(apt, c) == 3 && rd(apt, c + 8) == depth && (move < 0 || ((rd(apt, c + 4) & 1) != 0) == (move == 1))) return c;
    throw Error("no placement at depth " + std::to_string(depth) + " in frame " + std::to_string(frame));
}

uint32_t Movie::place(uint32_t depth, int char_id, uint32_t flags, float tx, float ty, float scale, const std::string &name)
{
    uint32_t name_off = name.empty() ? 0 : cstring(name);
    Bytes b(64, 0);
    wr(b, 0, 3);
    wr(b, 4, flags);
    wr(b, 8, depth);
    wr(b, 12, (uint32_t)char_id);
    wrf(b, 16, scale);
    wrf(b, 28, scale);
    wrf(b, 32, tx);
    wrf(b, 36, ty);
    if (name_off) {
        wr(b, 4, flags | 0x20);
        wr(b, 52, name_off);
    }
    wr(b, 56, 0xFFFFFFFFu); // clip depth -1: not a mask
    return alloc(b.data(), b.size());
}

void Movie::prepend_action(uint32_t stream)
{
    uint32_t ctl = do_action(stream);
    std::vector<uint32_t> ctls = controls(main, 0);
    ctls.insert(ctls.begin(), ctl);
    set_controls(main, 0, ctls);
    not_main_.insert(ctl);
}

uint32_t Movie::do_action(uint32_t stream)
{
    uint32_t w[2] = {1, stream};
    return alloc(w, 8);
}

uint32_t Movie::clone_text(uint32_t text, const std::string &initial, const std::string &variable)
{
    if (rd(apt, text) != 2) throw Error("not a text character");
    Bytes b(apt.begin() + text, apt.begin() + text + 60);
    wr(b, 52, cstring(initial));
    wr(b, 56, cstring(variable));
    return alloc(b.data(), b.size());
}

uint32_t Movie::clone_sprite(uint32_t sprite, const std::map<int, int> &remap)
{
    if (rd(apt, sprite) != 5) throw Error("not a sprite");
    uint32_t nframes = rd(apt, sprite + 8);
    std::vector<uint32_t> frames;
    for (uint32_t f = 0; f < nframes; f++) {
        std::vector<uint32_t> ctls;
        for (uint32_t c : controls(sprite, (int)f)) {
            uint32_t t = rd(apt, c);
            if (t == 3) {
                Bytes b(apt.begin() + c, apt.begin() + c + 64);
                if (rd(b, 4) & 2) {
                    auto it = remap.find((int)rd(b, 12));
                    if (it != remap.end()) wr(b, 12, (uint32_t)it->second);
                }
                if ((rd(b, 4) & 128) && rd(b, 60)) throw Error("cannot clone clip actions");
                ctls.push_back(alloc(b.data(), b.size()));
            } else if (t == 1) {
                uint32_t s = rd(apt, c + 4);
                if (s + 2 > apt.size() || apt[s] != 7 || apt[s + 1] != 0) throw Error("cannot clone a script other than stop()");
                const uint8_t stop[2] = {7, 0};
                ctls.push_back(do_action(alloc(stop, 2)));
            } else {
                throw Error("cannot clone control type " + std::to_string(t));
            }
        }
        frames.push_back((uint32_t)ctls.size());
        frames.push_back(ctls.empty() ? 0 : alloc_words(ctls));
    }
    uint32_t fa = alloc_words(frames);
    return alloc_words({5, kSig, nframes, fa, 0});
}

uint32_t Movie::main_script() const
{
    for (uint32_t c : controls(main, 0))
        if (rd(apt, c) == 1 && !not_main_.count(c)) return rd(apt, c + 4);
    throw Error("no main script");
}

void Movie::rename_function(uint32_t pos, const std::string &from, const std::string &to)
{
    for (int guard = 0; guard < 1000000; guard++) {
        if (pos >= apt.size()) break;
        uint8_t op = apt[pos];
        if (op == 0) break;
        if (op == 142 || op == 155) {
            uint32_t p = align4(pos + 1);
            if (str(rd(apt, p)) == from) {
                wr(apt, p, cstring(to));
                return;
            }
            pos = p + (op == 142 ? 28 : 24); // the body follows inline
            continue;
        }
        pos = skip_op(apt, pos);
    }
    throw Error("no function " + from);
}

std::vector<std::string> Movie::define_classes_on_load()
{
    std::vector<std::string> names;
    for (uint32_t f = 0; f < rd(apt, main + 8); f++) {
        std::vector<uint32_t> ctls = controls(main, (int)f), defs;
        for (uint32_t ctl : ctls) {
            if (rd(apt, ctl) != 8) continue;
            uint32_t pos = rd(apt, ctl + 8);
            uint8_t prev1 = 0, prev2 = 0;
            std::vector<std::string> dict, pushed; // the stream's DefineDictionary; the strings it pushes
            auto text = [&](uint32_t item) -> std::string {
                uint32_t idx = rd(apt, item);
                if (idx >= consts_.size()) return {};
                auto [type, value] = consts_[idx];
                if (type == 8) return value < dict.size() ? dict[value] : std::string();
                if (type != 1) return {};
                std::string s;
                for (uint32_t p = value; p < cst_.size() && cst_[p]; p++) s += (char)cst_[p];
                return s;
            };
            for (int i = 0; i < 12 && pos < apt.size() && apt[pos]; i++) {
                uint8_t op = apt[pos];
                if (op == 136 || op == 150) {
                    uint32_t p = align4(pos + 1), cnt = rd(apt, p), arr = rd(apt, p + 4);
                    for (uint32_t k = 0; k < cnt; k++) (op == 136 ? dict : pushed).push_back(text(arr + 4 * k));
                }
                if (op == 0x9D && prev1 == 0x12 && prev2 == 0x12) {
                    // if (!_global.X) { ... }: branch to the next op, and run it as the frame's DoAction
                    if (pushed.size() < 2 || pushed[0] != "_global") throw Error("a class definition without _global.<name>");
                    wr(apt, align4(pos + 1), 0);
                    wr(apt, ctl, 1);
                    wr(apt, ctl + 4, rd(apt, ctl + 8));
                    names.push_back(pushed[1]);
                    defs.push_back(ctl);
                    not_main_.insert(ctl);
                    break;
                }
                prev2 = prev1;
                prev1 = op;
                pos = skip_op(apt, pos);
            }
        }
        if (defs.empty()) continue;
        // before the frame's first DoAction, as DoInitActions run before the frame's actions
        std::vector<uint32_t> order;
        bool placed = false;
        for (uint32_t ctl : ctls) {
            if (std::find(defs.begin(), defs.end(), ctl) != defs.end()) continue;
            if (!placed && rd(apt, ctl) == 1) {
                order.insert(order.end(), defs.begin(), defs.end());
                placed = true;
            }
            order.push_back(ctl);
        }
        if (!placed) order.insert(order.end(), defs.begin(), defs.end());
        set_controls(main, (int)f, order);
    }
    return names;
}

int Movie::add_const(const Value &v)
{
    new_.push_back(v);
    return (int)(consts_.size() + new_.size() - 1);
}

void Movie::walk_movie(uint32_t c, std::vector<uint32_t> &items) const
{
    uint32_t nframes = rd(apt, c + 8);
    for (uint32_t f = 0; f < nframes; f++)
        for (uint32_t ctl : controls(c, (int)f)) {
            uint32_t t = rd(apt, ctl);
            if (t == 1) walk_stream(rd(apt, ctl + 4), items);
            else if (t == 8) walk_stream(rd(apt, ctl + 8), items);
            else if (t == 3 && (rd(apt, ctl + 4) & 128) && rd(apt, ctl + 60)) {
                uint32_t acts = rd(apt, ctl + 60), nev = rd(apt, acts), aev = rd(apt, acts + 4);
                for (uint32_t e = 0; e < nev; e++) walk_stream(rd(apt, aev + 12 * e + 8), items);
            }
        }
}

void Movie::walk_stream(uint32_t pos, std::vector<uint32_t> &items) const
{
    for (int guard = 0; guard < 10000000; guard++) {
        if (pos >= apt.size()) throw Error("ActionScript runs past the movie");
        uint8_t op = apt[pos];
        if (op == 0) return;
        if (op == 136 || op == 150) {
            uint32_t p = align4(pos + 1), cnt = rd(apt, p), arr = rd(apt, p + 4);
            for (uint32_t i = 0; i < cnt; i++) items.push_back(arr + 4 * i);
        }
        pos = skip_op(apt, pos);
    }
    throw Error("endless ActionScript");
}

Bytes Movie::build()
{
    std::vector<uint32_t> items;
    for (uint32_t c : characters())
        if (c && (rd(apt, c) == 5 || rd(apt, c) == 9)) walk_movie(c, items);
    Bytes cst = cst_;
    std::vector<uint32_t> table;
    for (size_t k = 0; k < items.size(); k++) {
        uint32_t v = rd(apt, items[k]);
        if (v < consts_.size()) {
            table.push_back(consts_[v].first);
            table.push_back(consts_[v].second);
        } else if (v - consts_.size() < new_.size()) {
            const Value &n = new_[v - consts_.size()];
            if (n.type == 1) {
                table.push_back(1);
                table.push_back((uint32_t)cst.size());
                cst.insert(cst.end(), n.text.begin(), n.text.end());
                cst.push_back(0);
            } else {
                table.push_back(n.type);
                table.push_back(n.raw);
            }
        } else {
            throw Error("a script item refers to no constant");
        }
        wr(apt, items[k], (uint32_t)k);
    }
    pad(cst, 4);
    uint32_t tpos = (uint32_t)cst.size();
    cst.resize(cst.size() + 4 * table.size());
    if (!table.empty()) memcpy(cst.data() + tpos, table.data(), 4 * table.size());
    wr(cst, 0x18, (uint32_t)items.size());
    wr(cst, 0x1C, tpos);
    pad(apt, 16);
    Bytes hdr = hdr_;
    uint32_t c = 0x80 + (uint32_t)apt.size();
    pad(cst, 16);
    wr(hdr, 0x10, c);
    Bytes tail = tail_;
    if (!tail.empty()) {
        // geometry and what follows (texture table +0x14, block-relative inside; texture offsets +8, region-relative,
        // count +4; texture data 0x80-aligned) move together by a multiple of 0x80
        uint32_t old_g = rd(hdr_, 0x18);
        while ((c + cst.size()) % 0x80 != old_g % 0x80) cst.push_back(0);
        uint32_t delta = c + (uint32_t)cst.size() - old_g;
        wr(hdr, 0x18, old_g + delta);
        if (uint32_t t = rd(hdr, 0x14)) {
            if (t < old_g) throw Error("texture table before the geometry");
            wr(hdr, 0x14, t + delta);
        }
        uint32_t cnt = rd(hdr, 4), arr = rd(hdr, 8);
        if (arr) {
            if (arr < old_g) throw Error("texture offsets before the geometry");
            wr(hdr, 8, arr + delta);
            for (uint32_t i = 0; i < cnt; i++) wr(tail, arr - old_g + 4 * i, rd(tail, arr - old_g + 4 * i) + delta);
        }
    } else {
        wr(hdr, 0x18, 0);
    }
    Bytes out = hdr;
    out.insert(out.end(), apt.begin(), apt.end());
    out.insert(out.end(), cst.begin(), cst.end());
    out.insert(out.end(), tail.begin(), tail.end());
    return out;
}

// ---------------------------------------------------------------------------------------------------- Asm
Asm::Asm(Movie &mv) : mv_(mv)
{
    pad(mv_.apt, 4);
    base_ = (uint32_t)mv_.apt.size();
}

void Asm::align4()
{
    while ((base_ + b_.size()) % 4) b_.push_back(0);
}

Asm &Asm::op(uint8_t code)
{
    b_.push_back(code);
    return *this;
}

Asm &Asm::push(const std::vector<Value> &items)
{
    std::vector<uint32_t> idx;
    for (const Value &v : items) idx.push_back((uint32_t)mv_.add_const(v));
    b_.push_back(150);
    align4();
    size_t at = b_.size();
    b_.resize(b_.size() + 8, 0);
    uint32_t n = (uint32_t)idx.size();
    memcpy(b_.data() + at, &n, 4);
    items_.push_back({at + 4, idx});
    return *this;
}

Asm &Asm::function2(const std::string &name, const std::function<void(Asm &)> &body, uint16_t flags, uint16_t regs)
{
    b_.push_back(142);
    align4();
    size_t hdr = b_.size();
    b_.resize(b_.size() + 28, 0);
    size_t start = b_.size();
    body(*this);
    uint32_t size = (uint32_t)(b_.size() - start);
    // {name, nparams, regs (u16), flags (u16), params, code size, 0x98765432, 0x12345678}: the runtime keeps
    // per-function state in the last two words while the movie is loaded and puts the markers back when it unloads
    uint32_t marks[2] = {0x98765432u, 0x12345678u};
    memcpy(b_.data() + hdr + 8, &regs, 2);
    memcpy(b_.data() + hdr + 10, &flags, 2);
    memcpy(b_.data() + hdr + 16, &size, 4);
    memcpy(b_.data() + hdr + 20, marks, 8);
    functions_.push_back({hdr, name});
    return *this;
}

size_t Asm::branch(uint8_t code)
{
    b_.push_back(code);
    align4();
    size_t at = b_.size();
    b_.resize(b_.size() + 4, 0);
    return at;
}

void Asm::land(size_t at)
{
    int32_t off = (int32_t)(b_.size() - (at + 4)); // from the end of the operand
    memcpy(b_.data() + at, &off, 4);
}

uint32_t Asm::finish()
{
    b_.push_back(0);
    if (mv_.apt.size() != base_) throw Error("apt data changed while assembling");
    mv_.apt.insert(mv_.apt.end(), b_.begin(), b_.end());
    for (auto &[at, idx] : items_) wr(mv_.apt, base_ + at, mv_.alloc_words(idx)); // item arrays after the stream
    for (auto &[hdr, name] : functions_) {
        wr(mv_.apt, base_ + hdr, mv_.cstring(name));
        const uint32_t none[2] = {0, 0}; // parameter list: empty, never null in the files
        wr(mv_.apt, base_ + hdr + 12, mv_.alloc(none, 8));
    }
    return base_;
}

} // namespace apt
