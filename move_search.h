#pragma once

#include "board.h"

enum Level {
  kLevel18,
  kLevel19,
  kLevel29,
  kLevel39
};

constexpr int GetRow(int frame, Level level) {
  switch (level) {
    case kLevel18: return frame / 3;
    case kLevel19: return frame / 2;
    case kLevel29: return frame;
    case kLevel39: return frame * 2;
  }
  __builtin_unreachable();
}

constexpr bool IsDropFrame(int frame, Level level) {
  switch (level) {
    case kLevel18: return frame % 3 == 2;
    case kLevel19: return frame % 2 == 1;
    default: return true;
  }
}

constexpr int GetLastFrameOnRow(int row, Level level) {
  switch (level) {
    case kLevel18: return row * 3 + 2;
    case kLevel19: return row * 2 + 1;
    case kLevel29: return row;
    case kLevel39: return row;
  }
  __builtin_unreachable();
}

template <
    int tap1, int tap2, int tap3, int tap4, int tap5,
    int tap6, int tap7, int tap8, int tap9, int tap10>
class TapTable {
  int t[10];
 public:
  constexpr TapTable() : t{tap1, tap2, tap3, tap4, tap5, tap6, tap7, tap8, tap9, tap10} {
    static_assert(
        tap1 >= 0 && tap2 >= 2 && tap3 >= 2 && tap4 >= 2 && tap5 >= 2 &&
        tap6 >= 2 && tap7 >= 2 && tap8 >= 2 && tap9 >= 2 && tap10 >= 2,
        "each tap must be at least 2 frames apart");
    for (int i = 1; i < 10; i++) t[i] += t[i - 1];
  }
  constexpr int operator[](int x) const { return t[x]; }
};

constexpr int abs(int x) { return x < 0 ? -x : x; }
constexpr int sgn(int x) {
  return x == 0 ? 0 : x > 0 ? 1 : -1;
}

// Check each bit in mask is set in board
template <int R>
constexpr bool Contains(const std::array<Board, R>& board, const std::array<Board, R>& mask) {
  bool ret = true;
  for (int i = 0; i < R; i++) ret &= (board[i] & mask[i]) == mask[i];
  return ret;
}

template <int R>
struct TableEntry {
  // Each entry corresponds to a (rot, col) that is possible to reach
  // `num_taps` is the minimum number of taps needed to reach that (rot, col)
  //   (so taps[num_taps-1] will be the exact frame when the piece is moved to (rot, col))
  // This (rot, col) is only reachable if the cells corresponding to `masks` in entry `prev`
  //   (prev is regarded recursively; that is, the `prev` of `prev`, etc., should also be included)
  //   and `masks_nodrop` in this entry are all empty
  //   (`masks_nodrop` should be a subset of `masks` in the same entry)
  uint8_t rot, col, prev, num_taps;
  // `cannot_finish` means no further input is possible since it will be locked at the bottom of the board
  // when it happens, `masks` should not be used
  bool cannot_finish;
  std::array<Board, R> masks, masks_nodrop;
};

template <Level level, int R, int... tap_args>
constexpr int Phase1TableGen(int initial_frame, int initial_rot, int initial_col, TableEntry<R> entries[]) {
  int sz = 0;
  static_assert(R == 1 || R == 2 || R == 4, "unexpected rotations");
  constexpr uint8_t kA = 0x1;
  constexpr uint8_t kB = 0x2;
  constexpr uint8_t kL = 0x4;
  constexpr uint8_t kR = 0x8;
  TapTable<tap_args...> taps;
  Board masks[R][10][R] = {};
  Board masks_nodrop[R][10][R] = {};
  uint8_t last_tap[R][10] = {};
  bool cannot_reach[R][10] = {};
  bool cannot_finish[R][10] = {};
  for (int i = 0; i < R; i++) {
    for (int j = 0; j < 10; j++) {
      for (int k = 0; k < R; k++) masks[i][j][k] = Board(0, 0, 0, 0);
    }
  }
  for (int col = 0; col < 10; col++) {
    for (int delta_rot = 0; delta_rot < 4; delta_rot++) {
      // piece end up at column col and rotation (initial_rot + delta_rot)
      if (R == 1 && delta_rot != 0) continue;
      if (R == 2 && delta_rot >= 2) continue;
      int rot = (initial_rot + delta_rot) % R;
      int num_lr_tap = abs(col - initial_col);
      int num_ab_tap = delta_rot == 3 ? 1 : delta_rot; // [0,1,2,1]
      int num_tap = std::max(num_ab_tap, num_lr_tap);
      // the frame that this tap occurred; initial_frame if no input
      int start_frame = (num_tap == 0 ? 0 : taps[num_tap - 1]) + initial_frame;
      // the frame that next input is allowed
      int end_frame = taps[num_tap] + initial_frame;
      if (num_tap) {
        if (num_tap == num_lr_tap) last_tap[rot][col] |= col > initial_col ? kR : kL;
        if (num_tap == num_ab_tap) last_tap[rot][col] |= delta_rot == 3 ? kB : kA;
      }
      // the position before this tap
      int start_row = GetRow(start_frame, level);
      if (start_row >= 20) {
        cannot_reach[rot][col] = true;
        continue;
      }
      int start_col = num_tap == num_lr_tap ? col - sgn(col - initial_col) : col;
      int start_rot = num_tap == num_ab_tap ? ((delta_rot == 2 ? 1 : 0) + initial_rot) % R : rot;
      auto& cur = masks[rot][col];
      cur[start_rot].Set(start_row, start_col);
      cur[start_rot].Set(start_row, col); // first shift
      cur[rot].Set(start_row, col); // then rotate
      masks_nodrop[rot][col] = cur;
      if (GetRow(end_frame, level) >= 20) {
        cannot_finish[rot][col] = true;
        continue;
      }
      for (int frame = start_frame; frame < end_frame; frame++) {
        int row = GetRow(frame, level);
        cur[rot].Set(row, col);
        if (IsDropFrame(frame, level)) {
          cur[rot].Set(row + 1, col);
          if (level == kLevel39) cur[rot].Set(row + 2, col);
        }
      }
    }
  }
  // start from (initial_col, initial_row) and build the entries according to
  //   ascending tap count
  auto Push = [&](uint8_t rot, uint8_t col, uint8_t prev, uint8_t num_taps) {
    if (!cannot_reach[rot][col]) {
      entries[sz] = {rot, col, prev, num_taps, cannot_finish[rot][col], {}, {}};
      for (int i = 0; i < R; i++) {
        entries[sz].masks[i] = masks[rot][col][i];
        entries[sz].masks_nodrop[i] = masks_nodrop[rot][col][i];
      }
      sz++;
    }
  };
  Push(initial_rot, initial_col, 0, 0);
  for (int cur = 0; cur < sz; cur++) {
    int rot = entries[cur].rot, col = entries[cur].col, taps = entries[cur].num_taps;
    int last = last_tap[rot][col];
    bool should_l = col > 0 && (taps == 0 || (last & kL));
    bool should_r = col < 9 && (taps == 0 || (last & kR));
    bool should_a = (R > 1 && taps == 0) || (R == 4 && taps == 1 && (last & kA));
    bool should_b = R == 4 && taps == 0;
    if (should_l) Push(rot, col - 1, cur, taps + 1); // L
    if (should_r) Push(rot, col + 1, cur, taps + 1); // R
    if (should_a) {
      int nrot = (rot + 1) % R;
      Push(nrot, col, cur, taps + 1); // A
      if (should_l) Push(nrot, col - 1, cur, taps + 1); // L
      if (should_r) Push(nrot, col + 1, cur, taps + 1); // R
    }
    if (should_b) {
      int nrot = (rot + 3) % R;
      Push(nrot, col, cur, taps + 1); // A
      if (should_l) Push(nrot, col - 1, cur, taps + 1); // L
      if (should_r) Push(nrot, col + 1, cur, taps + 1); // R
    }
  }
  return sz;
}

template <Level level, int R, int adj_frame, int... tap_args>
struct Phase1Table {
  static constexpr int initial_N = Phase1TableGen<level, R, tap_args...>(
      0, 0, 5, std::array<TableEntry<R>, 10*R>().data());
  TableEntry<R> initial[initial_N];
  int adj_N[initial_N];
  TableEntry<R> adj[initial_N][10 * R];
  constexpr Phase1Table() : initial{}, adj_N{}, adj{} {
    TapTable<tap_args...> taps;
    Phase1TableGen<level, R, tap_args...>(0, 0, 5, initial);
    for (int i = 0; i < initial_N; i++) {
      int frame_start = std::max(adj_frame, taps[initial[i].num_taps]);
      adj_N[i] = Phase1TableGen<level, R, tap_args...>(
          frame_start, initial[i].rot, initial[i].col, adj[i]);
    }
  }
};

// Column is simply a column; each bit LSB is topmost
// Frames is a processed form of a column; each bit is a frame, LSB is the first frame
// Frames comes in two flavors: normal and drop mask
//   normal mask just corresponds to the row the piece is in on each frame
//   drop mask is AND of all rows that the piece will pass when dropping
// For level 18,19,29, drop mask is just (normal_mask & normal_mask >> 1);
//   drop mask exists only to make it easy to deal with level 39
using Column = uint32_t;
using Frames = uint64_t;

struct FrameMasks {
  Frames frame, drop;
};

template <Level level>
constexpr Frames ColumnToNormalFrameMask(Column col) {
  switch (level) {
    case kLevel18: {
      constexpr uint64_t kMask = 0x249249249249249;
      uint64_t expanded = pdep<uint64_t>(col, kMask);
      return expanded | expanded << 1 | expanded << 2;
    }
    case kLevel19: {
      constexpr uint64_t kMask = 0x5555555555;
      uint64_t expanded = pdep<uint64_t>(col, kMask);
      return expanded | expanded << 1;
    }
    case kLevel29: return col;
    case kLevel39: {
      constexpr uint32_t kMask = 0x55555;
      return pext(col, kMask);
    }
  }
  __builtin_unreachable();
}

template <Level level>
constexpr Frames ColumnToDropFrameMask(Column col) {
  switch (level) {
    case kLevel18: [[fallthrough]];
    case kLevel19: [[fallthrough]];
    case kLevel29: {
      uint64_t mask = ColumnToNormalFrames(col);
      return mask & mask >> 1;
    }
    case kLevel39: {
      constexpr uint32_t kMask = 0x55555;
      return pext(col & col >> 1 & col >> 2, kMask);
    }
  }
  __builtin_unreachable();
}

template <Level level>
constexpr FrameMasks ColumnToFrameMasks(Column col) {
  return {ColumnToNormalFrameMask(col), ColumnToDropFrameMask(col)};
}

template <Level level>
constexpr Column FramesToColumn(Frames frames) {
  switch (level) {
    case kLevel18: {
      constexpr uint64_t kMask = 0x249249249249249;
      return pext(frames | frames >> 1 | frames >> 2, kMask);
    }
    case kLevel19: {
      constexpr uint64_t kMask = 0x5555555555;
      return pext(frames | frames >> 1, kMask);
    }
    case kLevel29: return frames;
    case kLevel39: {
      constexpr uint32_t kMask = 0x55555;
      return pdep<uint32_t>(frames, kMask);
    }
  }
}

constexpr int FindLockRow(uint32_t col, int start_row) {
  // given (col & 1<<row) != 0
  // col               = 00111100011101
  // 1<<row            = 00000000001000
  // col+(1<<row)      = 00111100100101
  // col^(col+(1<<row))= 00000000111000
  //              highbit=31-clz ^
  return 31 - __builtin_clz(col ^ (col + (1 << row))) - 1;
}

// note: "tuck" here means tucks, spins or spintucks
struct TuckMask {
  int delta_rot, delta_col, delta_frame;
  Frames masks[R][10];
};

constexpr int TuckTypes(int R) {
  return R == 1 ? 2 : R == 2 ? 7 : 12;
  // R = 1: L R
  // R = 2: A LA RA AL AR
  // R = 4: B LB RB BL BR
  // it is possible to add other tuck types suck as buco-like spins
  // but we just keep it simple here
}

template <class R>
constexpr std::array<TuckMask, TuckTypes(R)> GetTuckMasks(FrameMasks m[R][10]) {
  std::array<TuckMask, TuckTypes(R)> ret{};
  ret[0] = {0, -1, 0, {}}; // L
  ret[1] = {0, 1, 0, {}}; // R
  for (int rot = 0; rot < R; rot++) {
    for (int col = 0; col < 10; col++) {
      if (col > 0) ret[0].masks[rot][col] = m[rot][col].frame & m[rot][col-1].frame;
      if (col < 9) ret[1].masks[rot][col] = m[rot][col].frame & m[rot][col+1].frame;
    }
  }
  if (R == 1) return ret;
  ret[2] = {1, 0, 0, {}}; // A
  ret[3] = {1, -1, 0, {}}; // LA
  ret[4] = {1, 1, 0, {}}; // RA
  ret[5] = {1, -1, 1, {}}; // AL
  ret[6] = {1, 1, 1, {}}; // AR
  for (int rot = 0; rot < R; rot++) {
    int nrot = (rot + 1) % R;
    for (int col = 0; col < 10; col++) {
      ret[2].masks[rot][col] = m[rot][col].frame & m[nrot][col].frame;
      if (col > 0) ret[3].masks[rot][col] = ret[0].masks[rot][col] & m[nrot][col-1].frame;
      if (col < 9) ret[4].masks[rot][col] = ret[1].masks[rot][col] & m[nrot][col+1].frame;
      if (col > 0) ret[5].masks[rot][col] = m[rot][col].frame & m[nrot][col].drop & m[nrot][col-1].frame >> 1;
      if (col > 9) ret[6].masks[rot][col] = m[rot][col].frame & m[nrot][col].drop & m[nrot][col+1].frame >> 1;
    }
  }
  if (R == 2) return ret;
  ret[7] = {3, 0, 0, {}}; // B
  ret[8] = {3, -1, 0, {}}; // LB
  ret[9] = {3, 1, 0, {}}; // RB
  ret[10] = {3, -1, 1, {}}; // BL
  ret[11] = {3, 1, 1, {}}; // BR
  for (int rot = 0; rot < R; rot++) {
    int nrot = (rot + 3) % R;
    for (int col = 0; col < 10; col++) {
      ret[7].masks[rot][col] = m[rot][col].frame & m[nrot][col].frame;
      if (col > 0) ret[8].masks[rot][col] = ret[0].masks[rot][col] & m[nrot][col-1].frame;
      if (col < 9) ret[9].masks[rot][col] = ret[1].masks[rot][col] & m[nrot][col+1].frame;
      if (col > 0) ret[10].masks[rot][col] = m[rot][col].frame & m[nrot][col].drop & m[nrot][col-1].frame >> 1;
      if (col > 9) ret[11].masks[rot][col] = m[rot][col].frame & m[nrot][col].drop & m[nrot][col+1].frame >> 1;
    }
  }
  return ret;
}


/** drop sequence:
 *
 * initial phase 1         adj phase 1
 * vvvvvvvvvvv               vvvvvvv
 * L - L - L - - - - - - - - R - R - - - - - - -<lock>
 *               \           ^ adj_frame   \
 *                \A R - - -<lock>          \B R - - -<lock>
 *                 ^^^^                      ^^^^
 *                initial phase 2 (tuck)    adj phase 2
 */
template <Level level, int R, int adj_frame, int... tap_args>
constexpr void MoveSearch(const std::array<Board, R>& board) {
  TapTable<tap_args...> taps;
  Phase1Table<level, R, adj_frame, tap_args...> table;
  constexpr int initial_N = decltype(table)::initial_N;
  Column cols[R][10] = {};
  FrameMasks frame_masks[R][10] = {};
  for (int rot = 0; rot < R; rot++) {
    for (int col = 0; col < 10; col++) {
      cols[rot][col] = board[rot].Column(col);
      frame_masks[rot][col] = ColumnToFrameMasks<level>(cols[rot][col]);
    }
  }
  auto tuck_masks = GetTuckMasks(frame_masks);
  Column reachable_without_tuck[R][10] = {}; // positions reachable without tucking
  bool can_adj[initial_N] = {}; // whether adjustment starting from this (rot, col) is possible
  int adj_row = GetRow(adj_frame, level);
  { // initial, phase 1
    bool can_continue[initial_N] = {}; // whether the next tap can continue
    Frames can_tuck_frame_masks[R][10] = {}; // frames that can start a tuck
    for (int i = 0; i < initial_N; i++) {
      auto& entry = table.initial[i];
      if (!can_continue[entry.prev]) continue;
      if (!entry.cannot_finish && Contains(board, entry.masks)) {
        can_continue[i] = true;
      } else if (!Contains(board, entry.masks_nodrop)) {
        continue;
      }
      int start_row = GetRow(entry.num_taps == 0 ? 0 : taps[entry.num_taps - 1], level);
      int end_frame = std::max(adj_frame, taps[entry.num_taps]);
      int end_row = GetRow(end_frame); // end_row might >= 20
      // Since we verified masks_nodrop, start_row should be in col
      if ((col[entry.row][entry.col] & 1 << start_row) == 0) throw std::runtime_error("unexpected");
      int lock_row = FindLockRow(col[entry.row][entry.col], start_row);
      if (lock_row >= end_row) {
        can_adj[i] = true;
      } else {
        // TODO: add to list
      }
      int last_tuck_frame = std::min(GetLastFrameOnRow(lock_row) + 1, end_frame);
      can_tuck_frame_masks[entry.row][entry.col] =
          (1ll << last_tuck_frame) - (1ll << taps[entry.num_taps]);
      reachable_without_tuck[entry.row][entry.col] = (2 << lock_row) - (1 << start_row);
    }
    // initial, phase 2
    Frames tuck_result[R][10] = {};
    for (auto& tuck : tuck_masks) {
      int start_col = tuck.delta_col == -1 ? 1 : 0;
      int end_col = tuck.delta_col == 1 ? 9 : 10;
      for (int rot = 0; rot < R; rot++) {
        int nrot = (rot + tuck.delta_rot) % R;
        for (int col = start_col; col < end_col; col++) {
          tuck_result[nrot][col + tuck.delta_col] |=
              (tuck.masks[rot][col] & can_tuck_frame_masks[rot][col]) << tuck.delta_frame;
        }
      }
    }
    for (int rot = 0; rot < R; rot++) {
      for (int col = 0; col < 10; col++) {
        Column after_tuck_positions = FramesToColumn(
            tuck_result[rot][col] & ~reachable_without_tuck[rot][col]);
        Column cur = cols[row][col];
        Column tuck_lock_positions = (after_tuck_positions + cur) >> 1 & (cur & ~cur >> 1);
        // TODO: parse and add to list
      }
    }
  }
  for (int initial_id = 0; initial_id < initial_N; initial_id++) {
    // adj, phase 1
    int N = table.adj_N[initial_id];
    // TODO: do the same thing
  }
}