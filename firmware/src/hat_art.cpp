// Adapted from agents-deck (github.com/brushknight/agents-deck) @ 699bba8, firmware/devices/ws169-panel/src/hats.c.
// Ported from it with the onigiri frames dropped (Claude's critter only); keep in step with the original.
#include "hat_art.h"

#include <stddef.h>

static const char *const f0_rows[] = {"....aaaaa.....", "...acaaaaa....", "..aaaaaaaaa...", ".bbbbbbbbbbbb."};
static const char *const f2_rows[] = {"....aaaaa.....", "...acaaaaa....", "..aaaaccaaa...", ".bbbbbbbbbbbb."};
static const char *const f3_rows[] = {"....aaaaa.....", "...acaaaaa....", "..aaaabbaaa...", ".bbbbbbbbbbbb."};
static const HatFrame hat0_critter[] = {{0, -4, 4, f2_rows, 0, NULL}, {0, -4, 4, f3_rows, 0, NULL}};
static const char *const f6_rows[] = {"........aaaaaa", ".......aaaaacaa", "bbbbbbbbbbbbbbb"};
static const char *const f8_rows[] = {"........aaaaaa", ".......aaaaacaa", "bbbbbbbbbbbbbbb"};
static const char *const f9_rows[] = {"........aaaaaa", ".......aaaaacaa", "bbbbbbbbbbbbbbb"};
static const char *const f10_rows[] = {"........aaaaaa", ".......aaaaacaa", "...bbbbbbbbbbbb"};
static const HatExtra f10_extras[] = {{-3, 0, 'b'}, {-2, 0, 'b'}};
static const char *const f11_rows[] = {"........aaaaaa", ".......aaaaacaa", "bbbbbbbbbbbbbbb"};
static const HatFrame hat1_critter[] = {{-3, -3, 3, f8_rows, 0, NULL}, {-3, -3, 3, f9_rows, 0, NULL}, {-3, -3, 3, f10_rows, 2, f10_extras}, {-3, -3, 3, f11_rows, 0, NULL}};
static const char *const f16_rows[] = {".....cc.......", "....aaaaaa....", "...aaaaaaaa...", "..babababab..."};
static const char *const f18_rows[] = {".....cc.......", "....aaaaaa....", "...aaaaaaaa...", "..babababab..."};
static const char *const f19_rows[] = {"....aaaaaa....", "...aaaaaaaa...", "..babababab..."};
static const HatExtra f19_extras[] = {{5, -5, 'c'}, {6, -5, 'c'}};
static const HatFrame hat2_critter[] = {{0, -4, 4, f18_rows, 0, NULL}, {0, -3, 3, f19_rows, 2, f19_extras}};
static const char *const f22_rows[] = {"....aaaaaa....", "...aaaaaaaa...", "..aaaacaaaaa..", "..bbbbbbbbbb.."};
static const char *const f24_rows[] = {"....aaaaaa....", "...aaaaaaaa...", "..acaaaaaaaa..", "..bbbbbbbbbb.."};
static const char *const f25_rows[] = {"....aaaaaa....", "...aaaaaaaa...", "..aaaaacaaaa..", "..bbbbbbbbbb.."};
static const char *const f26_rows[] = {"....aaaaaa....", "...aaaaaaaa...", "..aaaaaaaaca..", "..bbbbbbbbbb.."};
static const char *const f27_rows[] = {"....aaaaaa....", "...aaaaaaaa...", "..aaaaaaaaaa..", "..bbbbbbbbbb.."};
static const HatFrame hat3_critter[] = {{0, -4, 4, f24_rows, 0, NULL}, {0, -4, 4, f25_rows, 0, NULL}, {0, -4, 4, f26_rows, 0, NULL}, {0, -4, 4, f27_rows, 0, NULL}};
static const char *const f32_rows[] = {"........a.....", ".......aa.....", "......aaca....", ".....aaaaaa...", "..bbbbbbbbbb.."};
static const char *const f34_rows[] = {"........a.....", ".......aa.....", "......aaca....", ".....aaaaaa...", "..bbbbbbbbbb.."};
static const char *const f35_rows[] = {".........a....", ".......aa.....", "......aaca....", ".....aaaaaa...", "..bbbbbbbbbb.."};
static const HatExtra f35_extras[] = {{11, -6, 'c'}};
static const char *const f36_rows[] = {"........a.....", ".......aa.....", "......aaca....", ".....aaaaaa...", "..bbbbbbbbbb.."};
static const char *const f37_rows[] = {".......a......", ".......aa.....", "......aaca....", ".....aaaaaa...", "..bbbbbbbbbb.."};
static const HatFrame hat4_critter[] = {{0, -5, 5, f34_rows, 0, NULL}, {0, -5, 5, f35_rows, 1, f35_extras}, {0, -5, 5, f36_rows, 0, NULL}, {0, -5, 5, f37_rows, 0, NULL}};
static const char *const f42_rows[] = {"......b.......", "...aaaaaaaa...", "..aaaaaaaaaaa.", "...bbbbbbbb..."};
static const char *const f44_rows[] = {"......b.......", "...aaaaaaaa...", "..aaaaaaaaaaa.", "...bbbbbbbb..."};
static const char *const f45_rows[] = {".......b......", "...aaaaaaaa...", "..aaaaaaaaaaa.", "...bbbbbbbb..."};
static const char *const f46_rows[] = {"......b.......", "...aaaaaaaa...", "..aaaaaaaaaaa.", "...bbbbbbbb..."};
static const char *const f47_rows[] = {".....b........", "...aaaaaaaa...", "..aaaaaaaaaaa.", "...bbbbbbbb..."};
static const HatFrame hat5_critter[] = {{0, -4, 4, f44_rows, 0, NULL}, {0, -4, 4, f45_rows, 0, NULL}, {0, -4, 4, f46_rows, 0, NULL}, {0, -4, 4, f47_rows, 0, NULL}};
static const char *const f52_rows[] = {"...cc.cc.cc...", "..cccccccccc..", "...cccccccc...", "...aaaaaaaa..."};
static const char *const f54_rows[] = {"...cc.cc.cc...", "..cccccccccc..", "...cccccccc...", "...aaaaaaaa..."};
static const char *const f55_rows[] = {"....cccccc....", "..cccccccccc..", "..cccccccccc..", "...cccccccc...", "...aaaaaaaa..."};
static const HatFrame hat6_critter[] = {{0, -4, 4, f54_rows, 0, NULL}, {0, -5, 5, f55_rows, 0, NULL}};
static const char *const f58_rows[] = {"...a..aa..a...", "...aa.aa.aa...", "...aaaaaaaa...", "...acaacaaca.."};
static const char *const f60_rows[] = {"...c..aa..a...", "...aa.aa.aa...", "...aaaaaaaa...", "...acaacaaca.."};
static const char *const f61_rows[] = {"...a..ca..a...", "...aa.aa.aa...", "...aaaaaaaa...", "...acaacaaca.."};
static const char *const f62_rows[] = {"...a..ac..a...", "...aa.aa.aa...", "...aaaaaaaa...", "...acaacaaca.."};
static const char *const f63_rows[] = {"...a..aa..a...", "...aa.aa.aa...", "...aaaaaaaa...", "...acaacaaca.."};
static const HatFrame hat7_critter[] = {{0, -4, 4, f60_rows, 0, NULL}, {0, -4, 4, f61_rows, 0, NULL}, {0, -4, 4, f62_rows, 0, NULL}, {0, -4, 4, f63_rows, 0, NULL}};
static const char *const f68_rows[] = {"....aaaaaa....", "...a......a...", "..a........a.."};
static const char *const f70_rows[] = {"....aaaaaa....", "...a......a...", "..a........a.."};
static const HatExtra f70_extras[] = {{-2, -1, 'c'}, {-1, -2, 'c'}};
static const char *const f71_rows[] = {"....aaaaaa....", "...a......a...", "..a........a.."};
static const HatExtra f71_extras[] = {{-2, -2, 'c'}, {-1, -3, 'c'}};
static const char *const f72_rows[] = {"....aaaaaa....", "...a......a...", "..a........a.."};
static const HatExtra f72_extras[] = {{-3, -3, 'c'}, {-2, -4, 'c'}, {-1, -1, 'c'}};
static const char *const f73_rows[] = {"....aaaaaa....", "...a......a...", "..a........a.."};
static const HatExtra f73_extras[] = {{-2, -2, 'c'}};
static const HatFrame hat8_critter[] = {{0, -3, 3, f70_rows, 2, f70_extras}, {0, -3, 3, f71_rows, 2, f71_extras}, {0, -3, 3, f72_rows, 3, f72_extras}, {0, -3, 3, f73_rows, 1, f73_extras}};
static const char *const f78_rows[] = {"..cccc.aaaa...", ".......d......", "....ababab....", "...aaaaaaaa..."};
static const char *const f80_rows[] = {"..cccc.aaaa...", ".......d......", "....ababab....", "...aaaaaaaa..."};
static const char *const f81_rows[] = {"....cc.aa.....", ".......d......", "....ababab....", "...aaaaaaaa..."};
static const char *const f82_rows[] = {"..aaaa.cccc...", ".......d......", "....ababab....", "...aaaaaaaa..."};
static const char *const f83_rows[] = {"....aa.cc.....", ".......d......", "....ababab....", "...aaaaaaaa..."};
static const HatFrame hat9_critter[] = {{0, -4, 4, f80_rows, 0, NULL}, {0, -4, 4, f81_rows, 0, NULL}, {0, -4, 4, f82_rows, 0, NULL}, {0, -4, 4, f83_rows, 0, NULL}};
static const char *const f88_rows[] = {"...aaaaaaaa...", "...aaaaaaaa...", "...aaaaaaaa...", "...bbbbbbbb...", ".aaaaaaaaaaaa."};
static const char *const f90_rows[] = {"...aaaaaaaa...", "...aaaaaaaa...", "...aaaaaaaa...", "...bbbbbbbb...", ".aaaaaaaaaaaa."};
static const char *const f91_rows[] = {"...aaaaaaaa...", "...aaaaaaaa...", "...aaaaaaaa...", "...bbbbbbbb...", ".aaaaaaaaaaaa."};
static const char *const f92_rows[] = {"...aaaaaaaa...", "...aaaaaaaa...", "...aaaaaaaa...", "...bbbbbbbb...", ".aaaaaaaaaaaa."};
static const char *const f93_rows[] = {"...aaaaaaaa...", "...aaaaaaaa...", "...aaaaaaaa...", "...bbbbbbbb...", ".aaaaaaaaaaaa."};
static const HatFrame hat10_critter[] = {{0, -5, 5, f90_rows, 0, NULL}, {0, -5, 5, f91_rows, 0, NULL}, {0, -6, 5, f92_rows, 0, NULL}, {0, -5, 5, f93_rows, 0, NULL}};
static const char *const f98_rows[] = {".....aaaa.....", "....aabbaa....", "b...aaaaaa...b", "bbbbbbbbbbbbbb"};
static const char *const f100_rows[] = {".....aaaa.....", "....aabbaa....", "b...aaaaaa...b", "bbbbbbbbbbbbbb"};
static const char *const f101_rows[] = {".....aaaa.....", "b...aabbaa....", "b...aaaaaa....", ".bbbbbbbbbbbbb"};
static const char *const f102_rows[] = {".....aaaa.....", "....aabbaa....", "b...aaaaaa...b", "bbbbbbbbbbbbbb"};
static const char *const f103_rows[] = {".....aaaa.....", "....aabbaa...b", "....aaaaaa...b", "bbbbbbbbbbbbb."};
static const HatFrame hat11_critter[] = {{0, -4, 4, f100_rows, 0, NULL}, {0, -4, 4, f101_rows, 0, NULL}, {0, -4, 4, f102_rows, 0, NULL}, {0, -4, 4, f103_rows, 0, NULL}};
static const char *const f108_rows[] = {"..aaaaaaaaaa..", "..acacacacac.."};
static const HatExtra f108_extras[] = {{1, -1, 'b'}, {0, -2, 'b'}, {0, 0, 'b'}, {-1, 0, 'b'}};
static const char *const f110_rows[] = {"..aaaaaaaaaa..", "..acacacacac.."};
static const HatExtra f110_extras[] = {{1, -1, 'b'}, {0, -2, 'b'}, {0, 0, 'b'}, {-1, 0, 'b'}};
static const char *const f111_rows[] = {"..aaaaaaaaaa..", "..acacacacac.."};
static const HatExtra f111_extras[] = {{1, -1, 'b'}, {0, -1, 'b'}, {-1, -2, 'b'}, {0, 0, 'b'}, {-1, 1, 'b'}};
static const char *const f112_rows[] = {"..aaaaaaaaaa..", "..acacacacac.."};
static const HatExtra f112_extras[] = {{1, -1, 'b'}, {0, -2, 'b'}, {-1, -2, 'b'}, {0, 0, 'b'}, {-1, 0, 'b'}};
static const HatFrame hat12_critter[] = {{0, -1, 2, f110_rows, 4, f110_extras}, {0, -1, 2, f111_rows, 5, f111_extras}, {0, -1, 2, f112_rows, 5, f112_extras}};

const HatArt HAT_ART[] = {
    {"hard hat", 400, {0, -4, 4, f0_rows, 0, NULL}, 2, hat0_critter},
    {"cap", 300, {-3, -3, 3, f6_rows, 0, NULL}, 4, hat1_critter},
    {"beanie", 350, {0, -4, 4, f16_rows, 0, NULL}, 2, hat2_critter},
    {"helmet", 280, {0, -4, 4, f22_rows, 0, NULL}, 4, hat3_critter},
    {"wizard", 300, {0, -5, 5, f32_rows, 0, NULL}, 4, hat4_critter},
    {"beret", 300, {0, -4, 4, f42_rows, 0, NULL}, 4, hat5_critter},
    {"chef", 450, {0, -4, 4, f52_rows, 0, NULL}, 2, hat6_critter},
    {"crown", 280, {0, -4, 4, f58_rows, 0, NULL}, 4, hat7_critter},
    {"headphones", 320, {0, -3, 3, f68_rows, 0, NULL}, 4, hat8_critter},
    {"propeller", 120, {0, -4, 4, f78_rows, 0, NULL}, 4, hat9_critter},
    {"top hat", 450, {0, -5, 5, f88_rows, 0, NULL}, 4, hat10_critter},
    {"cowboy", 350, {0, -4, 4, f98_rows, 0, NULL}, 4, hat11_critter},
    {"bandana", 200, {0, -1, 2, f108_rows, 4, f108_extras}, 3, hat12_critter},
};
const int HAT_ART_COUNT = 13;
