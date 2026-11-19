module

/-!
# IoVecCursor

For `peregrine::internal::IoVecCursor` in `src/internal/lib/iovec_cursor.h`,
together with the `IoVec` helpers it uses from `src/internal/util/util.h`.

Modelling choices:

* Pointers are modelled as natural-number addresses, with `0` as `nullptr`.
  `cur_` is modelled as an index into `iovs`, and `end_` as `iovs.size`.
* `size_t` values are modelled as `Nat`, and `ssize_t` is assumed to be 64-bit.
  When the unwrapped total length is below `2^63` and `bytes ≤ bytesLeft`, no
  `size_t` operation in the C++ wraps, so the model computes the same values.
  Outside that range the C++ wraps around where `Nat` arithmetic does not, and
  the model says nothing about the C++ behaviour.
* `DCHECK`s are not executed. They are stated as hypotheses and conclusions of
  the theorems at the end of this file.
* The cursor is a pure value: `advance` returns the updated cursor instead of
  mutating in place. The C++ copy/move restrictions have no counterpart, the
  const and non-const overloads of `Head` and `RemainingSpan` collapse into
  one, and mutation of the items through the non-const overloads is not
  modelled.
-/

@[expose] public section

namespace Peregrine.Internal

/-- Model of `struct iovec`: a buffer address (`0` is `nullptr`) and a length. -/
structure IoVec where
  /-- `iov_base`. -/
  base : Nat
  /-- `iov_len`. -/
  len : Nat
  deriving Repr, DecidableEq

/-- `std::numeric_limits<ssize_t>::max()` on a 64-bit target. -/
def ssizeMax : Nat := 2 ^ 63 - 1

/-- `IsValid(const IoVec&)`: non-null base pointer and non-zero length. -/
def IoVec.isValid (v : IoVec) : Bool :=
  v.base != 0 && v.len >= 1

/-- `IsValid(absl::Span<const IoVec>)`: all the `iovecs` are valid. -/
def isValid (iovecs : Array IoVec) : Bool :=
  iovecs.all IoVec.isValid

/-- `TotalLength(absl::Span<const IoVec>)`: the total length of the `iovecs`. -/
def totalLength (iovecs : Array IoVec) : Nat :=
  iovecs.foldl (fun sum v => sum + v.len) 0

/-- A cursor for a sequence of `IoVec`s. All the provided iovecs must be valid:
non-null base pointers and non-zero lengths. -/
structure IoVecCursor where
  /-- `iovs_`: the cursor's own copy of the iovec items. -/
  iovs : Array IoVec
  /-- `cur_`, as an index into `iovs`. `end_` is `iovs.size`. -/
  cur : Nat
  /-- `bytes_left_`. -/
  bytesLeft : Nat
  /-- `bytes_total_`. -/
  bytesTotal : Nat
  deriving Repr, DecidableEq

namespace IoVecCursor

/-- Constructor (with input span in `[1, 2^63 - 1]` bytes). -/
def ofIoVecs (iovs : Array IoVec) : IoVecCursor :=
  { iovs
    cur := 0
    bytesLeft := totalLength iovs
    bytesTotal := totalLength iovs }

/-- Returns a span of the remaining iovec items. -/
def remainingSpan (c : IoVecCursor) : Array IoVec :=
  c.iovs.extract c.cur c.iovs.size

/-- Returns true iff the remaining iovec items are valid. -/
def isValid (c : IoVecCursor) : Bool :=
  Internal.isValid c.remainingSpan

/-- Returns the original total number of iovec items. -/
def totalItems (c : IoVecCursor) : Nat :=
  c.iovs.size

/-- Returns the original total number of bytes spanned by the iovec items. -/
def totalBytes (c : IoVecCursor) : Nat :=
  c.bytesTotal

/-- Returns the number of remaining bytes to be advanced. -/
def remainingBytes (c : IoVecCursor) : Nat :=
  c.bytesLeft

/-- Returns the number of remaining iovec items to be advanced. -/
def remainingItems (c : IoVecCursor) : Nat :=
  c.iovs.size - c.cur

/-- Returns the current first iovec, or `none` (`nullptr`) if exhausted. -/
def head (c : IoVecCursor) : Option IoVec :=
  if h : c.cur < c.iovs.size then some c.iovs[c.cur] else none

/-- The slow path of `advance`: skips fully consumed iovec items starting at
`cur`, then trims the first partially consumed one. Returns the updated items
and the new value of `cur`. -/
def advanceLoop (iovs : Array IoVec) (cur bytes : Nat) : Array IoVec × Nat :=
  if h : cur < iovs.size then
    let len := iovs[cur].len
    if bytes < len then
      (iovs.set cur { base := iovs[cur].base + bytes, len := len - bytes }, cur)
    else
      advanceLoop iovs (cur + 1) (bytes - len)
  else
    (iovs, cur)
termination_by iovs.size - cur

/-- Advances the cursor by `bytes`, which must not go beyond the end
(`1 ≤ bytes ≤ c.bytesLeft`). This call can update some iovec items but won't
delete any. Returns the updated cursor, and true iff all the iovec items are
exhausted.

For `bytes > c.bytesLeft` the C++ subtraction wraps around, whereas here it
saturates at zero and takes the fast path. -/
def advance (c : IoVecCursor) (bytes : Nat) : IoVecCursor × Bool :=
  -- Fast path for full iovec advances.
  let bytesLeft := c.bytesLeft - bytes
  if bytesLeft = 0 then
    ({ c with cur := c.iovs.size, bytesLeft }, true)
  else
    -- Slow path for partial iovec advances.
    let (iovs, cur) := advanceLoop c.iovs c.cur bytes
    ({ c with iovs, cur, bytesLeft }, !decide (cur < c.iovs.size))

/-- Returns a string representation of the cursor. -/
protected def toString (c : IoVecCursor) : String :=
  s!"IoVecCursor(index/size: {c.cur}/{c.iovs.size}, \
     bytes_left/total: {c.bytesLeft}/{c.bytesTotal})"

instance : ToString IoVecCursor := ⟨IoVecCursor.toString⟩

/-- Returns true if the cursor is in a valid state.

The C++ conjuncts `iovs_.data() <= cur_` and
`end_ == iovs_.data() + iovs_.size()` hold by construction in this model. -/
def invariant (c : IoVecCursor) : Bool :=
  Internal.isValid c.iovs && c.cur <= c.iovs.size &&
    c.bytesLeft <= c.bytesTotal &&
    c.bytesLeft == totalLength c.remainingSpan &&
    c.bytesLeft <= ssizeMax

end IoVecCursor

/-! ## Contracts

The `DCHECK`s of `IoVecCursor`, stated as theorems about the model. -/

/-- Total length of the items of `iovs` from index `cur` on. -/
private def suffixLength (iovs : Array IoVec) (cur : Nat) : Nat :=
  totalLength (iovs.extract cur iovs.size)

private theorem foldl_len (l : List IoVec) (a : Nat) :
    l.foldl (fun sum v => sum + v.len) a = a + l.foldl (fun sum v => sum + v.len) 0 := by
  induction l generalizing a with
  | nil => simp
  | cons x xs ih =>
    simp only [List.foldl_cons]
    rw [ih (a + x.len), ih (0 + x.len)]
    omega

private theorem suffixLength_eq (iovs : Array IoVec) (cur : Nat) :
    suffixLength iovs cur = (iovs.toList.drop cur).foldl (fun sum v => sum + v.len) 0 := by
  unfold suffixLength totalLength
  rw [← Array.foldl_toList]
  simp only [Array.toList_extract, List.extract_eq_take_drop]
  rw [List.take_of_length_le (by simp)]

private theorem suffixLength_of_lt {iovs : Array IoVec} {cur : Nat} (h : cur < iovs.size) :
    suffixLength iovs cur = iovs[cur].len + suffixLength iovs (cur + 1) := by
  rw [suffixLength_eq, suffixLength_eq, List.drop_eq_getElem_cons (by simpa using h),
    List.foldl_cons, foldl_len]
  simp

private theorem suffixLength_of_ge {iovs : Array IoVec} {cur : Nat} (h : iovs.size ≤ cur) :
    suffixLength iovs cur = 0 := by
  rw [suffixLength_eq, List.drop_of_length_le (by simpa using h)]
  rfl

private theorem suffixLength_set {iovs : Array IoVec} {cur : Nat} (h : cur < iovs.size)
    (v : IoVec) :
    suffixLength (iovs.set cur v) cur = v.len + suffixLength iovs (cur + 1) := by
  have h' : cur < (iovs.set cur v).size := by simpa using h
  rw [suffixLength_of_lt h', suffixLength_eq, suffixLength_eq]
  simp [List.drop_set_of_lt]

/-- Element-wise form of `Internal.isValid`. -/
private theorem isValid_iff (iovs : Array IoVec) :
    Internal.isValid iovs = true ↔ ∀ (i : Nat) (h : i < iovs.size), iovs[i].isValid = true := by
  simp [Internal.isValid, Array.all_eq_true]

/-- Specification of the slow path: if fewer than the remaining bytes are
consumed, the loop stops at an in-range item, keeps every item valid, and
shortens the remaining length by exactly `bytes`. -/
private theorem advanceLoop_spec (iovs : Array IoVec) (cur bytes : Nat)
    (hvalid : ∀ (i : Nat) (h : i < iovs.size), iovs[i].isValid = true)
    (hbytes : bytes < suffixLength iovs cur) :
    (IoVecCursor.advanceLoop iovs cur bytes).1.size = iovs.size ∧
    (IoVecCursor.advanceLoop iovs cur bytes).2 < iovs.size ∧
    (∀ (i : Nat) (h : i < (IoVecCursor.advanceLoop iovs cur bytes).1.size),
      (IoVecCursor.advanceLoop iovs cur bytes).1[i].isValid = true) ∧
    suffixLength (IoVecCursor.advanceLoop iovs cur bytes).1
      (IoVecCursor.advanceLoop iovs cur bytes).2 = suffixLength iovs cur - bytes := by
  fun_induction IoVecCursor.advanceLoop iovs cur bytes with
  | case1 cur bytes h len hlt =>
    refine ⟨by simp, h, ?_, ?_⟩
    · intro i hi
      have hv := hvalid cur h
      rw [Array.getElem_set]
      split
      · simp only [IoVec.isValid, Bool.and_eq_true, bne_iff_ne, ne_eq, ge_iff_le,
          decide_eq_true_eq] at hv ⊢
        omega
      · exact hvalid i (by simpa using hi)
    · rw [suffixLength_set h, suffixLength_of_lt h]
      simp only
      omega
  | case2 cur bytes h len hge ih =>
    rw [suffixLength_of_lt h] at hbytes ⊢
    have := ih (by omega)
    refine ⟨this.1, this.2.1, this.2.2.1, ?_⟩
    rw [this.2.2.2]
    omega
  | case3 cur bytes h =>
    rw [suffixLength_of_ge (by omega)] at hbytes
    omega

namespace IoVecCursor

/-- Propositional form of `invariant`. -/
private theorem invariant_iff (c : IoVecCursor) :
    c.invariant = true ↔
      (∀ (i : Nat) (h : i < c.iovs.size), c.iovs[i].isValid = true) ∧ c.cur ≤ c.iovs.size ∧
        c.bytesLeft ≤ c.bytesTotal ∧ c.bytesLeft = suffixLength c.iovs c.cur ∧
        c.bytesLeft ≤ ssizeMax := by
  simp [invariant, remainingSpan, suffixLength, isValid_iff, and_assoc]

/-- `DCHECK(invariant())` in the constructor: a cursor built from valid iovecs
spanning fewer than `2^63` bytes satisfies the invariant. -/
theorem invariant_ofIoVecs (iovs : Array IoVec) (hvalid : Internal.isValid iovs = true)
    (hlt : totalLength iovs < 2 ^ 63) : (ofIoVecs iovs).invariant = true := by
  rw [invariant_iff]
  refine ⟨(isValid_iff iovs).1 hvalid, Nat.zero_le _, Nat.le_refl _, ?_, ?_⟩
  · simp [ofIoVecs, suffixLength]
  · simp only [ofIoVecs, ssizeMax]
    omega

private theorem advance_fast (c : IoVecCursor) (bytes : Nat) (h : c.bytesLeft - bytes = 0) :
    c.advance bytes = ({ c with cur := c.iovs.size, bytesLeft := 0 }, true) := by
  simp [advance, h]

private theorem advance_slow (c : IoVecCursor) (bytes : Nat) (h : c.bytesLeft - bytes ≠ 0) :
    c.advance bytes =
      ({ c with
          iovs := (advanceLoop c.iovs c.cur bytes).1
          cur := (advanceLoop c.iovs c.cur bytes).2
          bytesLeft := c.bytesLeft - bytes },
        !decide ((advanceLoop c.iovs c.cur bytes).2 < c.iovs.size)) := by
  simp [advance, h]

/-- `DCHECK(invariant())` at the end of `Advance`: advancing within range
preserves the invariant.

`_hle` is the C++ precondition `DCHECK_LE(bytes, bytes_left_)`. The proof does
not use it, because `Nat` subtraction saturates; it is kept in the statement
because the model only matches the C++ within that range. The other
precondition, `DCHECK_GE(bytes, 1)`, is not needed. -/
theorem invariant_advance (c : IoVecCursor) (bytes : Nat) (hinv : c.invariant = true)
    (_hle : bytes ≤ c.bytesLeft) :
    (c.advance bytes).1.invariant = true := by
  rw [invariant_iff] at hinv ⊢
  obtain ⟨hvalid, hcur, htotal, hleft, hmax⟩ := hinv
  by_cases h : c.bytesLeft - bytes = 0
  · rw [advance_fast c bytes h]
    refine ⟨hvalid, Nat.le_refl _, Nat.zero_le _, ?_, Nat.zero_le _⟩
    exact (suffixLength_of_ge (Nat.le_refl c.iovs.size)).symm
  · rw [advance_slow c bytes h]
    obtain ⟨hsize, hlt, hvalid', hlen⟩ :=
      advanceLoop_spec c.iovs c.cur bytes hvalid (by omega)
    refine ⟨hvalid', ?_, ?_, ?_, ?_⟩ <;> simp only <;> omega

/-- `Advance` returns true iff all the iovec items are exhausted. On the slow
path this is `DCHECK(cur < end)`. See `invariant_advance` for `_hle`. -/
theorem advance_snd_iff (c : IoVecCursor) (bytes : Nat) (hinv : c.invariant = true)
    (_hle : bytes ≤ c.bytesLeft) :
    (c.advance bytes).2 = true ↔ (c.advance bytes).1.remainingItems = 0 := by
  rw [invariant_iff] at hinv
  obtain ⟨hvalid, hcur, htotal, hleft, hmax⟩ := hinv
  by_cases h : c.bytesLeft - bytes = 0
  · rw [advance_fast c bytes h]
    simp [remainingItems]
  · rw [advance_slow c bytes h]
    obtain ⟨hsize, hlt, -, -⟩ :=
      advanceLoop_spec c.iovs c.cur bytes hvalid (by omega)
    simp only [remainingItems, Bool.not_eq_true', decide_eq_false_iff_not]
    omega

end IoVecCursor

end Peregrine.Internal
