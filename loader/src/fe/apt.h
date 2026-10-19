// Editing BLACK's front-end containers (export/frontend/*.bin): EA "Apt" movies (a Flash-like format) with added
// characters, controls, constants and ActionScript. Errors throw apt::Error; nothing here knows about the pages.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace apt {

using Bytes = std::vector<uint8_t>;

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

uint32_t rd(const Bytes &b, size_t off);
void wr(Bytes &b, size_t off, uint32_t v);
float rdf(const Bytes &b, size_t off);
void wrf(Bytes &b, size_t off, float v);

// The file: entries (movie name -> region: a 0x80-byte header, then the movie), 0x80-aligned.
struct Container {
    std::vector<std::string> order;     // entry-table order
    std::map<std::string, Bytes> movies;
    void parse(const Bytes &data);
    Bytes build() const;
};

// A constant (ActionScript literal): 1 string, 6 float, 7 int (others are kept as read).
struct Value {
    uint32_t type;
    uint32_t raw;      // int / float bits / for an original string, its offset in the constant block
    std::string text;  // a new string
    static Value S(const std::string &s) { return {1, 0, s}; }
    static Value I(int32_t n) { return {7, (uint32_t)n, {}}; }
    static Value R(int reg) { return {4, (uint32_t)reg, {}}; } // a register
    static Value U() { return {3, 0, {}}; }                    // undefined
    static Value F(float f);
};

// Structure offsets (relative to the movie's apt data):
//  character {type, 0x09876543, ...}: 2 Text (60 bytes; initial text +52, variable +56), 5 Sprite / 9 Animation
//  {type, sig, nframes, aframes, labels}; Animation (char 0 = the main timeline) +20 nchars, +24 achars;
//  frame {ncontrols, acontrols}; control 1 DoAction {1, stream}, 3 PlaceObject (64 bytes: flags +4, depth +8,
//  char +12, matrix +16 (a b c d tx ty), colour transform +40, name +52, clip depth +56, clip actions +60),
//  8 DoInitAction {8, sprite, stream}.
class Movie {
public:
    explicit Movie(const Bytes &region);
    Bytes build(); // renumbers the constants in the runtime's walk order

    Bytes apt;     // the apt data (offsets in it are what the structures hold)
    uint32_t main; // the main timeline (character 0)

    uint32_t alloc(const void *data, size_t size, size_t align = 4);
    uint32_t alloc_words(const std::vector<uint32_t> &words);
    uint32_t cstring(const std::string &s);
    std::string str(uint32_t off) const;

    std::vector<uint32_t> characters() const;
    uint32_t character(int id) const; // throws unless it exists
    int add_character(uint32_t off);

    std::vector<uint32_t> controls(uint32_t sprite, int frame) const;
    void set_controls(uint32_t sprite, int frame, const std::vector<uint32_t> &ctls);
    void append_control(uint32_t sprite, int frame, uint32_t ctl);
    // the PlaceObject at depth in that frame (move: only a Move / only a new placement / either)
    uint32_t find_place(uint32_t sprite, int frame, uint32_t depth, int move = -1) const;
    uint32_t copy(uint32_t off, size_t size) { Bytes b(apt.begin() + off, apt.begin() + off + size); return alloc(b.data(), size); }
    uint32_t place(uint32_t depth, int char_id, uint32_t flags, float tx, float ty, float scale = 1.0f, const std::string &name = {});
    uint32_t do_action(uint32_t stream);
    // a DoAction that runs first in the main timeline's frame 0 (not main_script())
    void prepend_action(uint32_t stream);

    uint32_t clone_text(uint32_t text, const std::string &initial, const std::string &variable);
    // a sprite with fresh frames and controls; PlaceObject characters through remap; DoActions must be stop()
    uint32_t clone_sprite(uint32_t sprite, const std::map<int, int> &remap);
    uint32_t main_script() const; // the first DoAction stream of the main timeline's frame 0 (not one added by define_classes_on_load or prepend_action)
    void rename_function(uint32_t stream, const std::string &from, const std::string &to);
    // the main timeline's DoInitActions that define a class only when _global has none yet (if (!_global.X) {...}:
    // Not, Not, BranchIfTrue past the definition among the first ops) become DoActions that define it always: a
    // DoInitAction runs only when the runtime loads the movie, and a page entered again soon after it was left is
    // still loaded. Returns the classes' names.
    std::vector<std::string> define_classes_on_load();

    int add_const(const Value &v);

private:
    Bytes hdr_, cst_, tail_;
    std::vector<std::pair<uint32_t, uint32_t>> consts_; // the file's table
    std::vector<Value> new_;
    std::map<std::string, uint32_t> strings_;
    std::set<uint32_t> not_main_; // DoActions main_script() skips: class definitions, prepend_action
    void walk_movie(uint32_t c, std::vector<uint32_t> &items) const;
    void walk_stream(uint32_t pos, std::vector<uint32_t> &items) const;
};

// ActionScript for a new DoAction, assembled at its final offset (operands are aligned to absolute offsets).
class Asm {
public:
    explicit Asm(Movie &mv);
    Asm &op(uint8_t code);
    Asm &push(const std::vector<Value> &items); // each item gets a constant of its own
    Asm &push(const Value &v) { return push(std::vector<Value>{v}); }
    // DefineFunction2 without parameters; an empty name defines an anonymous function (pushed). flags 0x12A: register 1 =
    // _global; 0x29: register 1 = this; 0x129: register 1 = this, register 2 = _global.
    Asm &function2(const std::string &name, const std::function<void(Asm &)> &body, uint16_t flags = 0x12A, uint16_t regs = 4);
    size_t branch(uint8_t op); // BranchIfTrue (0x9D) / BranchAlways (0x99) to a later land()
    void land(size_t branch_at);
    uint32_t finish(); // the stream's offset

    Asm &get_var() { return op(0x1C); }
    Asm &set_var() { return op(0x1D); }
    Asm &get_member() { return op(0x4E); }
    Asm &set_member() { return op(0x4F); }
    Asm &new_object() { return op(0x40); }
    Asm &call_method() { return op(0x52); }
    Asm &call_function() { return op(0x3D); }
    Asm &pop() { return op(0x17); }
    Asm &ret() { return op(0x3E); }
    Asm &push_true() { return op(0x73); }
    Asm &define_local2() { return op(0x41); }
    Asm &delete2() { return op(0x3B); }
    Asm &type_of() { return op(0x44); }
    Asm &add2() { return op(0x47); }
    Asm &equals2() { return op(0x49); }
    Asm &logical_not() { return op(0x12); }

private:
    Movie &mv_;
    Bytes b_;
    uint32_t base_;
    std::vector<std::pair<size_t, std::vector<uint32_t>>> items_;
    std::vector<std::pair<size_t, std::string>> functions_;
    void align4();
};

} // namespace apt
