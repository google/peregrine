module

/-!
# TcpManagerBase

For `peregrine::internal::TcpManagerBase` in
`src/internal/socket/tcp_manager_base.{h,cc}`, together with the socket and
endpoint types it uses from `src/internal/socket/socket_{base,tcp,util}.h` and
`src/internal/base/{endpoint,types}.h`.

Modelling choices:

* `fd_t` is modelled as `Int`, and `absl::Duration` as `Int` (nanoseconds,
  with `0` as `absl::ZeroDuration()`).
* `Endpoint` is modelled abstractly by an IP address (`IpAddr`, IPv4 or IPv6
  with `0` as the zero address) and a `Nat` port (`port_t`, `uint16_t`).
* `std::unique_ptr<TcpSocket>` is modelled as `Option TcpSocket`, with `none`
  as `nullptr`. Operations that mutate containers (`Remove`, `Shutdown`,
  `Close`, `Seal`, `Add`, `RemoveExpired`, `MoveAll`) return the updated state
  instead of mutating in place, and `Close` / `MoveAll` / `RemoveExpired`
  also return the removed/doomed items.
* `absl::flat_hash_map<fd_t, V>` is modelled as an association list
  `List (Fd × V)` whose `invariant` requires distinct `fd` keys (`noDupKeys`).
  `std::vector<std::unique_ptr<TcpSocket>>` in `Produced` is modelled as
  `List (Option TcpSocket)`.
* Mutex locking (`absl::MutexLock`) and condition-variable waiting
  (`mu_.AwaitWithTimeout` in `Produced::MoveAll`) are not modelled: each
  method is modelled as a single sequential transition on the guarded state.
  `Produced.moveAll` models the post-wait state transfer (`std::exchange`).
* `DCHECK`s are not executed; they are stated as hypotheses and conclusions
  of the contract theorems below.
-/

@[expose] public section

namespace Peregrine.Internal

/-- Model of `fd_t` (`DEFINE_STRONG_INT_TYPE(fd_t, int)`). -/
abbrev Fd := Int

/-- Model of `absl::Duration` (signed nanoseconds; `0` is `ZeroDuration()`). -/
abbrev Duration := Int

/-- Model of `util::IpAddr` (IPv4 or IPv6 address; `0` represents the zero address). -/
inductive IpAddr where
  | ipv4 (addr : UInt32)
  | ipv6 (high low : UInt64)
  deriving Repr, DecidableEq

/-- `IpAddr::IsZero()`. -/
def IpAddr.isZero : IpAddr → Bool
  | .ipv4 a => a == 0
  | .ipv6 hi lo => hi == 0 && lo == 0

/-- Model of `peregrine::internal::Endpoint`. -/
structure Endpoint where
  /-- `ipaddr_`. -/
  ip : IpAddr
  /-- `port_`. -/
  port : Nat
  deriving Repr, DecidableEq

/-- `Endpoint::HasNonzeroIpPort()`. -/
def Endpoint.hasNonzeroIpPort (e : Endpoint) : Bool :=
  e.port != 0 && !e.ip.isZero

/-- Model of `peregrine::internal::TcpSocket` fields observable by
`TcpManagerBase`. -/
structure TcpSocket where
  /-- `SocketBase::fd_`. -/
  fd : Fd
  /-- `!SocketBase::blocking_`. -/
  nonBlocking : Bool
  /-- `SocketBase::connected_`. -/
  connected : Bool
  /-- Result of `SelfEndpoint(fd)` for this socket. -/
  selfEndpoint : Endpoint
  /-- Whether `TcpSocket::Shutdown()` has been called. -/
  shutDown : Bool := false
  deriving Repr, DecidableEq

/-- `IsPair(fd, s)` in `tcp_manager_base.cc`. -/
def isPair (fd : Fd) (s : Option TcpSocket) : Bool :=
  match s with
  | some sock => fd == sock.fd
  | none => false

/-- `IsPairAndNonBlocking(fd, s)` in `tcp_manager_base.cc`. -/
def isPairAndNonBlocking (fd : Fd) (s : Option TcpSocket) : Bool :=
  match s with
  | some sock => fd == sock.fd && sock.nonBlocking
  | none => false

/-! ## Association-list helpers for `absl::flat_hash_map<fd_t, V>` -/

namespace FdMap

/-- Returns true iff all `Fd` keys in `m` are distinct. -/
def noDupKeys {α : Type} : List (Fd × α) → Bool
  | [] => true
  | (k, _) :: rest => !rest.any (fun p => p.1 == k) && noDupKeys rest

/-- Lookup `fd` in `m` (`flat_hash_map::find`). -/
def find? {α : Type} (m : List (Fd × α)) (fd : Fd) : Option α :=
  (m.find? (fun p => p.1 == fd)).map Prod.snd

/-- Membership test (`flat_hash_map::contains`). -/
def contains {α : Type} (m : List (Fd × α)) (fd : Fd) : Bool :=
  m.any (fun p => p.1 == fd)

/-- Removes `fd` from `m` (`flat_hash_map::erase`). -/
def erase {α : Type} (m : List (Fd × α)) (fd : Fd) : List (Fd × α) :=
  m.filter (fun p => p.1 != fd)

end FdMap

/-! ## `TcpManagerBase::Listener` and `TcpManagerBase::Listeners` -/

/-- Model of `TcpManagerBase::Listener`. -/
structure Listener where
  /-- `socket`. -/
  socket : Option TcpSocket
  /-- `endpoint`: cached `SelfEndpoint(socket->fd())`. -/
  endpoint : Endpoint
  deriving Repr, DecidableEq

/-- Predicate checked by `Listeners::Invariant()` for each `(fd, l)` entry. -/
def Listener.entryValid (fd : Fd) (l : Listener) : Bool :=
  isPairAndNonBlocking fd l.socket &&
    match l.socket with
    | some sock => l.endpoint == sock.selfEndpoint && l.endpoint.hasNonzeroIpPort
    | none => false

/-- Model of `TcpManagerBase::Listeners`. -/
structure Listeners where
  /-- `fd2ls_`. -/
  fd2ls : List (Fd × Listener)
  deriving Repr, DecidableEq

namespace Listeners

/-- `Listeners::Invariant()` (together with map key uniqueness). -/
def invariant (ls : Listeners) : Bool :=
  FdMap.noDupKeys ls.fd2ls &&
    ls.fd2ls.all (fun p => Listener.entryValid p.1 p.2)

/-- `Listeners::Listeners(sockets)`. -/
def ofMap (sockets : List (Fd × Listener)) : Listeners :=
  { fd2ls := sockets }

/-- `Listeners::Get(fd)`. -/
def get (ls : Listeners) (fd : Fd) : Option TcpSocket :=
  (FdMap.find? ls.fd2ls fd).bind Listener.socket

/-- `Listeners::Find(endpoint)`. -/
def find (ls : Listeners) (ep : Endpoint) : Option Listener :=
  (ls.fd2ls.find? (fun p => p.2.endpoint == ep)).map Prod.snd

/-- `Listeners::Remove(fd)`. -/
def remove (ls : Listeners) (fd : Fd) : Listeners :=
  { fd2ls := FdMap.erase ls.fd2ls fd }

/-- `Listeners::Shutdown()`. -/
def shutdown (ls : Listeners) : Listeners :=
  { fd2ls := ls.fd2ls.map (fun p =>
      (p.1, { p.2 with socket := p.2.socket.map (fun s => { s with shutDown := true }) })) }

/-- `Listeners::Close()`. Returns the cleared `Listeners` and the closed
entries (`doomed`). -/
def close (ls : Listeners) : Listeners × List (Fd × Listener) :=
  ({ fd2ls := [] }, ls.fd2ls)

end Listeners

/-! ## `TcpManagerBase::Connector` and `TcpManagerBase::Connectors` -/

/-- Model of `TcpManagerBase::Connector`. -/
structure Connector where
  /-- `socket`. -/
  socket : Option TcpSocket
  /-- `expiration`. -/
  expiration : Duration
  deriving Repr, DecidableEq

/-- Model of `TcpManagerBase::Connectors`. -/
structure Connectors where
  /-- `sealed_`. -/
  sealed : Bool := false
  /-- `fd2cs_`. -/
  fd2cs : List (Fd × Connector) := []
  deriving Repr, DecidableEq

namespace Connectors

/-- `Connectors::Invariant()` (together with map key uniqueness). -/
def invariant (cs : Connectors) : Bool :=
  FdMap.noDupKeys cs.fd2cs &&
    cs.fd2cs.all (fun p => isPairAndNonBlocking p.1 p.2.socket)

/-- Stronger invariant capturing `!c.socket->IsConnected()`, checked by `DCHECK`
in `Add`, `Remove`, and `RemoveExpired`. -/
def allUnconnected (cs : Connectors) : Bool :=
  cs.fd2cs.all (fun p =>
    match p.2.socket with
    | some sock => !sock.connected
    | none => false)

/-- `Connectors::Seal()`. -/
def setSealed (cs : Connectors) : Connectors :=
  { cs with sealed := true }

/-- `Connectors::Add(fd, connector, on_add)`. Returns the updated `Connectors`
and true iff `connector` was added. -/
def add (cs : Connectors) (fd : Fd) (c : Connector) (onAdd : Fd → Bool) :
    Connectors × Bool :=
  if !cs.sealed && !FdMap.contains cs.fd2cs fd && onAdd fd then
    ({ cs with fd2cs := (fd, c) :: cs.fd2cs }, true)
  else
    (cs, false)

/-- `Connectors::Remove(fd)`. Returns the updated `Connectors` and the removed
socket (or `none` if `fd` was absent). -/
def remove (cs : Connectors) (fd : Fd) : Connectors × Option TcpSocket :=
  match FdMap.find? cs.fd2cs fd with
  | none => (cs, none)
  | some c => ({ cs with fd2cs := FdMap.erase cs.fd2cs fd }, c.socket)

/-- `Connectors::RemoveExpired(now)`. Returns the updated `Connectors` and all
removed connectors with `expiration ≤ now`. -/
def removeExpired (cs : Connectors) (now : Duration) : Connectors × List Connector :=
  let remaining := cs.fd2cs.filter (fun p => !(p.2.expiration ≤ now))
  let expired := (cs.fd2cs.filter (fun p => p.2.expiration ≤ now)).map Prod.snd
  ({ cs with fd2cs := remaining }, expired)

/-- `Connectors::Close()`. Returns the cleared `Connectors` and the doomed
entries. -/
def close (cs : Connectors) : Connectors × List (Fd × Connector) :=
  ({ cs with fd2cs := [] }, cs.fd2cs)

end Connectors

/-! ## `TcpManagerBase::Produced` -/

/-- Model of `TcpManagerBase::Produced`. -/
structure Produced where
  /-- `sealed_`. -/
  sealed : Bool := false
  /-- `sockets_`. -/
  sockets : List (Option TcpSocket) := []
  deriving Repr, DecidableEq

namespace Produced

/-- `Produced::Invariant()`. -/
def invariant (p : Produced) : Bool :=
  p.sockets.all (fun s =>
    match s with
    | some sock => sock.connected
    | none => false)

/-- `Produced::Seal()`. -/
def setSealed (p : Produced) : Produced :=
  { p with sealed := true }

/-- `Produced::Add(socket)`. Returns the updated `Produced` and true iff
`socket` was added. -/
def add (p : Produced) (s : Option TcpSocket) : Produced × Bool :=
  if !p.sealed then
    ({ p with sockets := p.sockets ++ [s] }, true)
  else
    (p, false)

/-- `Produced::hasSockets()`. -/
def hasSockets (p : Produced) : Bool :=
  !p.sockets.isEmpty

/-- `Produced::MoveAll(timeout)` (post-wait state transfer). -/
def moveAll (p : Produced) : Produced × List (Option TcpSocket) :=
  ({ p with sockets := [] }, p.sockets)

/-- `Produced::Close()`. Returns the cleared `Produced` and the doomed
sockets. -/
def close (p : Produced) : Produced × List (Option TcpSocket) :=
  ({ p with sockets := [] }, p.sockets)

end Produced

/-! ## Contracts

Invariants and `DCHECK` / postcondition theorems for `Listeners`, `Connectors`,
and `Produced`. -/

namespace FdMap

private theorem noDupKeys_filter {α : Type} (m : List (Fd × α)) (q : Fd × α → Bool)
    (h : noDupKeys m = true) : noDupKeys (m.filter q) = true := by
  induction m with
  | nil => simp [noDupKeys]
  | cons hd tl ih =>
    obtain ⟨k, v⟩ := hd
    simp only [noDupKeys, Bool.and_eq_true, Bool.not_eq_true'] at h
    have ih' := ih h.2
    simp only [List.filter_cons]
    split
    · rw [noDupKeys, Bool.and_eq_true, Bool.not_eq_true']
      refine ⟨?_, ih'⟩
      cases hq : (tl.filter q).any (fun p => p.1 == k)
      · rfl
      · exfalso
        rw [List.any_eq_true] at hq
        obtain ⟨x, hx, hxk⟩ := hq
        rw [List.mem_filter] at hx
        have htl : tl.any (fun p => p.1 == k) = true :=
          List.any_eq_true.mpr ⟨x, hx.1, hxk⟩
        rw [h.1] at htl
        cases htl
    · exact ih'

private theorem noDupKeys_erase {α : Type} (m : List (Fd × α)) (fd : Fd)
    (h : noDupKeys m = true) : noDupKeys (erase m fd) = true :=
  noDupKeys_filter m (fun p => p.1 != fd) h

private theorem noDupKeys_map_fst {α β : Type} (m : List (Fd × α)) (f : Fd → α → β)
    (h : noDupKeys m = true) : noDupKeys (m.map (fun p => (p.1, f p.1 p.2))) = true := by
  induction m with
  | nil => simp [noDupKeys]
  | cons hd tl ih =>
    obtain ⟨k, v⟩ := hd
    simp only [List.map_cons, noDupKeys, Bool.and_eq_true, Bool.not_eq_true'] at h ⊢
    refine ⟨?_, ih h.2⟩
    cases hq : (tl.map (fun p => (p.1, f p.1 p.2))).any (fun p => p.1 == k)
    · rfl
    · exfalso
      rw [List.any_eq_true] at hq
      obtain ⟨x, hx, hxk⟩ := hq
      rw [List.mem_map] at hx
      obtain ⟨y, hy, rfl⟩ := hx
      have htl : tl.any (fun p => p.1 == k) = true :=
        List.any_eq_true.mpr ⟨y, hy, hxk⟩
      rw [h.1] at htl
      cases htl

private theorem find?_mem {α : Type} {m : List (Fd × α)} {fd : Fd} {v : α}
    (h : find? m fd = some v) : ∃ p ∈ m, p.1 = fd ∧ p.2 = v := by
  simp only [find?, Option.map_eq_some_iff] at h
  obtain ⟨p, hp, rfl⟩ := h
  have hmem := List.mem_of_find?_eq_some hp
  have hpred := List.find?_some hp
  simp only [beq_iff_eq] at hpred
  exact ⟨p, hmem, hpred, rfl⟩

private theorem find?_erase_self {α : Type} (m : List (Fd × α)) (fd : Fd) :
    find? (erase m fd) fd = none := by
  simp only [find?, erase, Option.map_eq_none_iff, List.find?_eq_none]
  intro p hp
  rw [List.mem_filter] at hp
  simp only [bne_iff_ne, ne_eq] at hp
  simp [hp.2]

private theorem find?_erase_ne {α : Type} (m : List (Fd × α)) {fd fd' : Fd}
    (hne : fd' ≠ fd) : find? (erase m fd) fd' = find? m fd' := by
  induction m with
  | nil => simp [find?, erase]
  | cons hd tl ih =>
    obtain ⟨k, v⟩ := hd
    simp only [erase, List.filter_cons]
    by_cases hk : k = fd
    · subst hk
      simp only [bne_self_eq_false, Bool.false_eq_true, ↓reduceIte]
      have hne' : (k == fd') = false := beq_false_of_ne hne.symm
      change find? (erase tl k) fd' = find? ((k, v) :: tl) fd'
      rw [ih]
      simp [find?, hne']
    · have hbn : (k != fd) = true := bne_iff_ne.mpr hk
      simp only [hbn, ↓reduceIte]
      by_cases hk' : k = fd'
      · subst hk'
        simp [find?]
      · have hne' : (k == fd') = false := beq_false_of_ne hk'
        simp only [find?] at ih ⊢
        rw [List.find?_cons_of_neg (by simp [hne']), List.find?_cons_of_neg (by simp [hne'])]
        exact ih

private theorem contains_erase_self {α : Type} (m : List (Fd × α)) (fd : Fd) :
    contains (erase m fd) fd = false := by
  simp only [contains, erase, List.any_eq_false, List.mem_filter, bne_iff_ne, ne_eq,
    beq_iff_eq, and_imp]
  intro p _ hne
  exact hne

end FdMap

namespace Listeners

/-- `DCHECK(Invariant())` in `Listeners::Listeners(sockets)`. -/
theorem invariant_ofMap (sockets : List (Fd × Listener))
    (hdup : FdMap.noDupKeys sockets = true)
    (hvalid : sockets.all (fun p => Listener.entryValid p.1 p.2) = true) :
    (ofMap sockets).invariant = true := by
  simp [invariant, ofMap, hdup, hvalid]

/-- `Listeners::Get(fd)` returns a non-blocking socket with matching `fd`. -/
theorem get_spec {ls : Listeners} {fd : Fd} {sock : TcpSocket}
    (hinv : ls.invariant = true) (hget : ls.get fd = some sock) :
    sock.fd = fd ∧ sock.nonBlocking = true := by
  simp only [invariant, Bool.and_eq_true, List.all_eq_true] at hinv
  simp only [get, Option.bind_eq_some_iff] at hget
  obtain ⟨l, hfind, hsock⟩ := hget
  obtain ⟨p, hp, rfl, rfl⟩ := FdMap.find?_mem hfind
  have hv := hinv.2 p hp
  simp only [Listener.entryValid, isPairAndNonBlocking, hsock, Bool.and_eq_true,
    beq_iff_eq] at hv
  exact ⟨hv.1.1.symm, hv.1.2⟩

/-- `Listeners::Find(ep)` returns a listener with matching non-zero `endpoint`
and non-blocking socket. -/
theorem find_spec {ls : Listeners} {ep : Endpoint} {l : Listener}
    (hinv : ls.invariant = true) (hfind : ls.find ep = some l) :
    l.endpoint = ep ∧ l.endpoint.hasNonzeroIpPort = true ∧
      ∃ sock, l.socket = some sock ∧ sock.nonBlocking = true := by
  simp only [invariant, Bool.and_eq_true, List.all_eq_true] at hinv
  simp only [find, Option.map_eq_some_iff] at hfind
  obtain ⟨p, hp, rfl⟩ := hfind
  have hmem := List.mem_of_find?_eq_some hp
  have hpred := List.find?_some hp
  simp only [beq_iff_eq] at hpred
  have hv := hinv.2 p hmem
  cases hsock : p.2.socket with
  | none =>
    simp [Listener.entryValid, isPairAndNonBlocking, hsock] at hv
  | some sock =>
    simp only [Listener.entryValid, isPairAndNonBlocking, hsock, Bool.and_eq_true,
      beq_iff_eq] at hv
    exact ⟨hpred, hv.2.2, sock, rfl, hv.1.2⟩

/-- `Listeners::Remove(fd)` preserves `Invariant()`. -/
theorem invariant_remove (ls : Listeners) (fd : Fd) (hinv : ls.invariant = true) :
    (ls.remove fd).invariant = true := by
  simp only [invariant, remove, Bool.and_eq_true, List.all_eq_true] at hinv ⊢
  refine ⟨FdMap.noDupKeys_erase ls.fd2ls fd hinv.1, ?_⟩
  intro p hp
  rw [FdMap.erase, List.mem_filter] at hp
  exact hinv.2 p hp.1

/-- After `Listeners::Remove(fd)`, `Get(fd)` returns `none`, and other `fd'`
are unchanged. -/
theorem get_remove_self (ls : Listeners) (fd : Fd) :
    (ls.remove fd).get fd = none := by
  simp [get, remove, FdMap.find?_erase_self]

theorem get_remove_ne (ls : Listeners) {fd fd' : Fd} (hne : fd' ≠ fd) :
    (ls.remove fd).get fd' = ls.get fd' := by
  simp [get, remove, FdMap.find?_erase_ne _ hne]

/-- `Listeners::Shutdown()` preserves `Invariant()` and shuts down all
listener sockets. -/
theorem invariant_shutdown (ls : Listeners) (hinv : ls.invariant = true) :
    ls.shutdown.invariant = true := by
  simp only [invariant, shutdown, Bool.and_eq_true, List.all_eq_true] at hinv ⊢
  let f : Fd → Listener → Listener := fun _ l =>
    { l with socket := l.socket.map (fun s => { s with shutDown := true }) }
  refine ⟨FdMap.noDupKeys_map_fst ls.fd2ls f hinv.1, ?_⟩
  intro p hp
  rw [List.mem_map] at hp
  obtain ⟨q, hq, rfl⟩ := hp
  have hv := hinv.2 q hq
  cases hsock : q.2.socket with
  | none => simp [Listener.entryValid, isPairAndNonBlocking, hsock] at hv
  | some sock =>
    simp only [Listener.entryValid, isPairAndNonBlocking, hsock, Option.map_some,
      Bool.and_eq_true, beq_iff_eq] at hv ⊢
    exact hv

theorem shutdown_all_shutDown (ls : Listeners) (fd : Fd) (sock : TcpSocket)
    (hget : ls.shutdown.get fd = some sock) : sock.shutDown = true := by
  simp only [get, shutdown, Option.bind_eq_some_iff] at hget
  obtain ⟨l, hfind, hsock⟩ := hget
  obtain ⟨p, hp, _, hl⟩ := FdMap.find?_mem hfind
  rw [List.mem_map] at hp
  obtain ⟨q, _, hqeq⟩ := hp
  have hl' : l = { q.2 with socket := q.2.socket.map (fun s => { s with shutDown := true }) } := by
    rw [← hl, ← hqeq]
  rw [hl'] at hsock
  cases hqs : q.2.socket with
  | none => simp [hqs] at hsock
  | some s =>
    simp only [hqs, Option.map_some, Option.some.injEq] at hsock
    subst hsock
    rfl

/-- `Listeners::Close()` clears all listeners and satisfies `Invariant()`. -/
theorem close_spec (ls : Listeners) :
    (ls.close).1.invariant = true ∧
      (∀ fd, (ls.close).1.get fd = none) ∧
      (∀ ep, (ls.close).1.find ep = none) ∧
      (ls.close).2 = ls.fd2ls := by
  simp [close, invariant, FdMap.noDupKeys, get, FdMap.find?, find]

end Listeners

namespace Connectors

/-- The empty `Connectors` satisfies `Invariant()` and `allUnconnected`. -/
theorem invariant_empty : ({} : Connectors).invariant = true := by
  simp [invariant, FdMap.noDupKeys]

theorem allUnconnected_empty : ({} : Connectors).allUnconnected = true := by
  simp [allUnconnected]

/-- `Connectors::Seal()` sets `sealed` and preserves `Invariant()` and
`allUnconnected`. -/
theorem setSealed_spec (cs : Connectors) (hinv : cs.invariant = true) :
    cs.setSealed.sealed = true ∧ cs.setSealed.invariant = true := by
  simp [setSealed, invariant] at hinv ⊢
  exact hinv

theorem allUnconnected_setSealed (cs : Connectors) (hu : cs.allUnconnected = true) :
    cs.setSealed.allUnconnected = true :=
  hu

/-- `Connectors::Add(fd, connector, on_add)` preserves `Invariant()` when given a
valid non-blocking socket with matching `fd` (`DCHECK_EQ(fd, socket->fd())`). -/
theorem invariant_add (cs : Connectors) (fd : Fd) (c : Connector) (onAdd : Fd → Bool)
    (sock : TcpSocket) (hinv : cs.invariant = true)
    (hsock : c.socket = some sock) (hfd : sock.fd = fd) (hnb : sock.nonBlocking = true) :
    (cs.add fd c onAdd).1.invariant = true := by
  simp only [add]
  split
  · next hcond =>
    simp only [Bool.and_eq_true, Bool.not_eq_true'] at hcond
    simp only [invariant, Bool.and_eq_true, List.all_eq_true] at hinv ⊢
    refine ⟨?_, ?_⟩
    · simp only [FdMap.noDupKeys, Bool.and_eq_true, Bool.not_eq_true']
      exact ⟨hcond.1.2, hinv.1⟩
    · intro p hp
      rw [List.mem_cons] at hp
      rcases hp with rfl | hp
      · simp [isPairAndNonBlocking, hsock, hfd, hnb]
      · exact hinv.2 p hp
  · exact hinv

/-- `Connectors::Add` preserves `allUnconnected` under `DCHECK(!socket->IsConnected())`. -/
theorem allUnconnected_add (cs : Connectors) (fd : Fd) (c : Connector) (onAdd : Fd → Bool)
    (sock : TcpSocket) (hu : cs.allUnconnected = true)
    (hsock : c.socket = some sock) (hunc : sock.connected = false) :
    (cs.add fd c onAdd).1.allUnconnected = true := by
  simp only [add]
  split
  · simp only [allUnconnected, List.all_cons, hsock, hunc, Bool.not_false, Bool.true_and]
    exact hu
  · exact hu

/-- `Connectors::Add` rejects additions once `sealed_ = true`. -/
theorem add_of_sealed (cs : Connectors) (fd : Fd) (c : Connector) (onAdd : Fd → Bool)
    (hsealed : cs.sealed = true) :
    cs.add fd c onAdd = (cs, false) := by
  simp [add, hsealed]

/-- `Connectors::Add` rejects duplicate `fd` keys and `on_add == false`. -/
theorem add_of_contains (cs : Connectors) (fd : Fd) (c : Connector) (onAdd : Fd → Bool)
    (hcontains : FdMap.contains cs.fd2cs fd = true) :
    cs.add fd c onAdd = (cs, false) := by
  simp [add, hcontains]

theorem add_of_onAdd_false (cs : Connectors) (fd : Fd) (c : Connector) (onAdd : Fd → Bool)
    (hon : onAdd fd = false) :
    cs.add fd c onAdd = (cs, false) := by
  simp [add, hon]

/-- When `!sealed`, `!contains fd`, and `onAdd fd = true`, `Add` succeeds and
inserts `fd`. -/
theorem add_success (cs : Connectors) (fd : Fd) (c : Connector) (onAdd : Fd → Bool)
    (hsealed : cs.sealed = false) (hcontains : FdMap.contains cs.fd2cs fd = false)
    (hon : onAdd fd = true) :
    (cs.add fd c onAdd).2 = true ∧ FdMap.contains (cs.add fd c onAdd).1.fd2cs fd = true := by
  rw [add, hsealed, hcontains, hon]
  simp [FdMap.contains]

/-- `Connectors::Remove(fd)` preserves `Invariant()` and `allUnconnected`. -/
theorem invariant_remove (cs : Connectors) (fd : Fd) (hinv : cs.invariant = true) :
    (cs.remove fd).1.invariant = true := by
  simp only [remove]
  split
  · exact hinv
  · simp only [invariant, Bool.and_eq_true, List.all_eq_true] at hinv ⊢
    refine ⟨FdMap.noDupKeys_erase cs.fd2cs fd hinv.1, ?_⟩
    intro p hp
    rw [FdMap.erase, List.mem_filter] at hp
    exact hinv.2 p hp.1

theorem allUnconnected_remove (cs : Connectors) (fd : Fd) (hunc : cs.allUnconnected = true) :
    (cs.remove fd).1.allUnconnected = true := by
  simp only [remove]
  split
  · exact hunc
  · simp only [allUnconnected, List.all_eq_true] at hunc ⊢
    intro p hp
    rw [FdMap.erase, List.mem_filter] at hp
    exact hunc p hp.1

/-- `Connectors::Remove(fd)` removes `fd`, and any returned socket satisfies
`fd == sock.fd`, `sock.nonBlocking`, and (under `allUnconnected`) `!sock.connected`
(`DCHECK(!socket->IsConnected())`). -/
theorem remove_spec {cs : Connectors} {fd : Fd} {sock : TcpSocket}
    (hinv : cs.invariant = true) (hunc : cs.allUnconnected = true)
    (hrem : (cs.remove fd).2 = some sock) :
    FdMap.contains (cs.remove fd).1.fd2cs fd = false ∧
      sock.fd = fd ∧ sock.nonBlocking = true ∧ sock.connected = false := by
  simp only [invariant, Bool.and_eq_true, List.all_eq_true] at hinv
  simp only [allUnconnected, List.all_eq_true] at hunc
  simp only [remove] at hrem ⊢
  cases hfind : FdMap.find? cs.fd2cs fd with
  | none => simp [hfind] at hrem
  | some c =>
    simp only [hfind] at hrem ⊢
    obtain ⟨p, hp, rfl, rfl⟩ := FdMap.find?_mem hfind
    have hv := hinv.2 p hp
    have hu := hunc p hp
    simp only [isPairAndNonBlocking, hrem, Bool.and_eq_true, beq_iff_eq] at hv
    simp only [hrem, Bool.not_eq_true'] at hu
    exact ⟨FdMap.contains_erase_self _ _, hv.1.symm, hv.2, hu⟩

/-- `Connectors::RemoveExpired(now)` preserves `Invariant()` and `allUnconnected`,
removes exactly connectors with `expiration ≤ now`, and returns unconnected sockets
(`DCHECK(!c.socket->IsConnected())`). -/
theorem invariant_removeExpired (cs : Connectors) (now : Duration)
    (hinv : cs.invariant = true) :
    (cs.removeExpired now).1.invariant = true := by
  simp only [invariant, removeExpired, Bool.and_eq_true, List.all_eq_true] at hinv ⊢
  refine ⟨FdMap.noDupKeys_filter cs.fd2cs _ hinv.1, ?_⟩
  intro p hp
  rw [List.mem_filter] at hp
  exact hinv.2 p hp.1

theorem allUnconnected_removeExpired (cs : Connectors) (now : Duration)
    (hu : cs.allUnconnected = true) :
    (cs.removeExpired now).1.allUnconnected = true := by
  simp only [allUnconnected, removeExpired, List.all_eq_true] at hu ⊢
  intro p hp
  rw [List.mem_filter] at hp
  exact hu p hp.1

theorem removeExpired_spec (cs : Connectors) (now : Duration)
    (hu : cs.allUnconnected = true) :
    (∀ c ∈ (cs.removeExpired now).2,
      c.expiration ≤ now ∧ ∃ sock, c.socket = some sock ∧ sock.connected = false) ∧
    (∀ p ∈ (cs.removeExpired now).1.fd2cs, now < p.2.expiration) := by
  simp only [allUnconnected, List.all_eq_true] at hu
  simp only [removeExpired]
  refine ⟨?_, ?_⟩
  · intro c hc
    rw [List.mem_map] at hc
    obtain ⟨p, hp, rfl⟩ := hc
    rw [List.mem_filter] at hp
    have hexp : p.2.expiration ≤ now := by simpa using hp.2
    have hpu := hu p hp.1
    cases hsock : p.2.socket with
    | none => simp [hsock] at hpu
    | some sock =>
      simp only [hsock, Bool.not_eq_true'] at hpu
      exact ⟨hexp, sock, rfl, hpu⟩
  · intro p hp
    rw [List.mem_filter] at hp
    simp only [Bool.not_eq_true', decide_eq_false_iff_not] at hp
    exact Int.not_le.mp hp.2

/-- `Connectors::Close()` (called under `DCHECK(sealed_)`) empties `fd2cs`,
preserves `sealed`, and satisfies `Invariant()`. -/
theorem close_spec (cs : Connectors) (hsealed : cs.sealed = true) :
    (cs.close).1.invariant = true ∧
      (cs.close).1.allUnconnected = true ∧
      (cs.close).1.sealed = true ∧
      (cs.close).1.fd2cs = [] ∧
      (cs.close).2 = cs.fd2cs := by
  simp [close, invariant, allUnconnected, FdMap.noDupKeys, hsealed]

end Connectors

namespace Produced

/-- The empty `Produced` satisfies `Invariant()`. -/
theorem invariant_empty : ({} : Produced).invariant = true := by
  simp [invariant]

/-- `Produced::Seal()` sets `sealed` and preserves `Invariant()`. -/
theorem setSealed_spec (p : Produced) (hinv : p.invariant = true) :
    p.setSealed.sealed = true ∧ p.setSealed.invariant = true := by
  simp [setSealed, invariant] at hinv ⊢
  exact hinv

/-- `Produced::Add(socket)` preserves `Invariant()` under `DCHECK_NE(socket, nullptr)`
and `DCHECK(socket->IsConnected())`. -/
theorem invariant_add (p : Produced) (s : Option TcpSocket) (sock : TcpSocket)
    (hinv : p.invariant = true) (hs : s = some sock) (hconn : sock.connected = true) :
    (p.add s).1.invariant = true := by
  simp only [add]
  split
  · simp only [invariant, List.all_append, List.all_cons, List.all_nil, Bool.and_true,
      Bool.and_eq_true] at hinv ⊢
    exact ⟨hinv, by simp [hs, hconn]⟩
  · exact hinv

/-- `Produced::Add` rejects additions once `sealed_ = true`. -/
theorem add_of_sealed (p : Produced) (s : Option TcpSocket) (hsealed : p.sealed = true) :
    p.add s = (p, false) := by
  simp [add, hsealed]

/-- When `!sealed_`, `Produced::Add` appends `socket`, returns `true`, and
makes `hasSockets()` true. -/
theorem add_of_unsealed (p : Produced) (s : Option TcpSocket) (hsealed : p.sealed = false) :
    (p.add s).2 = true ∧ (p.add s).1.hasSockets = true := by
  simp [add, hsealed, hasSockets]

/-- `Produced::MoveAll()` empties `sockets_`, preserves `Invariant()`, and
returns all previously produced connected sockets. -/
theorem moveAll_spec (p : Produced) (hinv : p.invariant = true) :
    (p.moveAll).1.invariant = true ∧
      (p.moveAll).1.hasSockets = false ∧
      (p.moveAll).1.sealed = p.sealed ∧
      (p.moveAll).2 = p.sockets ∧
      (∀ s ∈ (p.moveAll).2, ∃ sock, s = some sock ∧ sock.connected = true) := by
  simp only [invariant, List.all_eq_true] at hinv
  simp only [moveAll, invariant, List.all_nil, hasSockets, List.isEmpty_nil,
    Bool.not_true, true_and]
  intro s hs
  have h := hinv s hs
  cases hsock : s with
  | none => simp [hsock] at h
  | some sock =>
    simp only [hsock] at h
    exact ⟨sock, rfl, h⟩

/-- `Produced::Close()` (called under `DCHECK(sealed_)`) empties `sockets_`,
preserves `sealed`, and satisfies `Invariant()`. -/
theorem close_spec (p : Produced) (hsealed : p.sealed = true) :
    (p.close).1.invariant = true ∧
      (p.close).1.hasSockets = false ∧
      (p.close).1.sealed = true ∧
      (p.close).2 = p.sockets := by
  simp [close, invariant, hasSockets, hsealed]

end Produced

/-! ## Sanity checks -/

private def sampleEp : Endpoint := ⟨.ipv4 0x7f000001, 8080⟩
private def sampleSock : TcpSocket :=
  { fd := 3, nonBlocking := true, connected := false, selfEndpoint := sampleEp }

#guard ({ fd2ls := [(3, ⟨some sampleSock, sampleEp⟩)] } : Listeners).invariant == true
#guard (({} : Connectors).add 3 ⟨some sampleSock, 100⟩ (fun _ => true)).2 == true
#guard (({} : Connectors).setSealed.add 3 ⟨some sampleSock, 100⟩ (fun _ => true)).2 == false
#guard (({} : Produced).add (some { sampleSock with connected := true })).2 == true

end Peregrine.Internal
