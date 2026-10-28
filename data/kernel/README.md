# BQ27220 capacity calibration driver extension

`0001-bq27220-capacity-calibration.patch` extends the board's existing
`bq27xxx_battery` driver. It targets
[lilygo-linux-dtoverlays revision 6f293e88735a7625f1a6677deb6afc7dc749373f](https://github.com/Xinyuan-LilyGO/lilygo-linux-dtoverlays/tree/6f293e88735a7625f1a6677deb6afc7dc749373f)
and changes `modules/bq27xxx/bq27xxx_battery.c` and its I2C transport. The Settings application
still builds independently; these kernel sources are not a build dependency.

The existing board driver provides read-only fuel-gauge statistics. Installing
this extension is required for the battery page's design/full capacity writes.
It does not enable a fictitious generic Linux calibration property.

## Install on the board

Apply the patch to a separate checkout of the driver repository, then build its
module against the **exact kernel version and configuration running on the
board**. The repository's module Makefile accepts `KDIR` and `CROSS_COMPILE`:

```sh
git apply --check /path/to/0001-bq27220-capacity-calibration.patch
git apply /path/to/0001-bq27220-capacity-calibration.patch
make -C modules/bq27xxx KDIR=/path/to/target/kernel/build \
    ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
```

Install the resulting `bq27xxx_battery.ko` using the board's normal kernel-module
deployment procedure, run `depmod`, and reboot during maintenance. Do not unload
an actively used gauge driver or bind a second I2C implementation to address
`0x55`. Board architecture and kernel release must match the resulting module.
The provided patch has not been installed or executed on hardware as part of
the Settings application change.

The fuel gauge's I2C adapter must expose a Device Tree `clock-frequency` of
100000 Hz or lower. Configure the actual adapter/controller node accordingly
and verify the running tree after reboot. Unknown, zero or faster clocks make
capacity writes return `EOPNOTSUPP` before changing device state. The patch
does not silently reconfigure the shared I2C bus. This restriction follows
TI's multi-byte-write requirement; the transport also inserts a 66 µs minimum
gap after each BQ27220 packet and rejects incomplete I2C reads.

## ABI

Only a BQ27220 with the required I2C bus operations exposes:

```text
/sys/class/power_supply/bq27220-N/calibration_capacity
```

The attribute has mode `0644` and the usual root ownership. A read returns
`DESIGN_MAH FULL_MAH\n`, using the ordinary DesignCapacity (`0x3c`) and
FullChargeCapacity (`0x12`) registers. Reads never unseal the gauge, enter config
mode, or write data. Full capacity is the current usable capacity reported by
the gauge, so normal capacity learning can change it after calibration.

A write accepts exactly two decimal integers in the same order. Both must be
between 1 and 32767 mAh, independently; full capacity may exceed design capacity.
Extra fields, overflow, zero, negative values and embedded NULs are rejected
before any device write. The Settings application uses its restricted helper
to perform an explicitly confirmed write; ordinary application processes do
not receive broad write access to all power-supply attributes.

The requested full capacity is the desired **visible usable capacity**. The
driver reads the existing Reserve Capacity and adds it to the RAM FCC seed;
the sum must also fit 32767 mAh. Reserve Capacity is preserved. A single
checksummed block updates only the FCC and design capacity fields, preserves
the remaining bytes, and is verified before reinitializing gauging. After
reinitialization, both ordinary registers must match the requested values.

The table's “CEDV Profile 1” RAM window is the working copy of the selected
profile; selecting another stored profile copies its contents into this same
RAM. This patch neither selects a profile nor changes Battery ID or the stored
OTP profiles. TI describes that copy behavior in its
[profile programming discussion](https://e2e.ti.com/support/power-management-group/power-management/f/power-management-forum/1392506/bq27220-otp-program-without-bqstudio).

## Security, lifetime and failure behavior

Only an intentional root store obtains FULL ACCESS. It uses TI's documented
default unseal and full-access key pairs once and verifies the security bits;
non-default keys produce an access error rather than attempts with other keys.
The original SEALED, UNSEALED or FULL ACCESS state is restored on every exit
where communication still works. A restore failure is returned and logged.
No keys or arbitrary register addresses are accepted from userspace.

An existing CONFIG UPDATE session, CALIBRATION mode, or qualified discharge
learning cycle returns `EBUSY`. The operation uses the driver's mutex for the
entire transaction, including security restoration. Ordinary property reads
and polling share that mutex. The transaction takes several seconds because
the gauge requires settling time.

BQ27220 ordinary power-supply reads and `calibration_capacity` reads use a
nonblocking lock attempt and return `EBUSY` while calibration owns the mutex.
This keeps GUI status-bar polling from waiting through the configuration
transaction. Consumers should keep the last successful reading and retry on
their next refresh. Other gauge models retain their existing locking behavior.

These are **volatile RAM parameters**. They survive application restarts but
can be lost when the gauge loses power. No OTP/ROM programming occurs, and the
application does not automatically reapply the values at every boot, which
would overwrite subsequently learned full capacity. Reapply intentionally after
gauge power loss if required. Changing these two capacities does not configure
a new battery chemistry, CEDV profile, voltage cutoff or charger voltage.

I2C or verification failures return errors. Once a write has been attempted,
the driver tries to restore and verify the original RAM block, exit config
mode, and restore security. This is **not an atomic hardware transaction**:
power loss or communication failure can prevent rollback. The kernel log
identifies cleanup failures; inspect and read back the gauge before retrying.
Rollback restores the original configured FCC seed, not a snapshot of the
gauge's entire learned state or remaining-charge history.

## Protocol sources and validation

The protocol follows the [TI BQ27220 Technical Reference Manual, SLUUBD4A](https://www.ti.com/lit/ug/sluubd4a/sluubd4a.pdf):

- Table 3-2: RAM FCC `0x929d`, design capacity `0x929f`, reserve `0x926d`;
  parameter values are big-endian.
- Sections 2.27 and 3.3: OperationStatus, security state and ROM unseal keys.
- Sections 2.29–2.31 and 6.1: MAC address, block checksum/length, configuration
  mode, reinitialization and verification. MAC addresses use little-endian.
- Section 4.9.36: the FCC seed is reduced by Reserve Capacity for usable FCC.
- Sections 3.1 and 8.1.4: RAM parameters are not persistent across power loss.
- Section 5.3: bus-free timing and the 100 kHz limit for multi-byte writes.

Section 6.1's example unseal sequence differs from the BQ27220 ROM key in
section 3.3; this patch uses the device-specific section 3.3 key and verifies
the resulting state. It waits at least two seconds after entering config mode
as required by section 2.2.21, rather than relying on shorter example timing.

TI also confirms that [FCC may be set manually through Data Memory](https://e2e.ti.com/support/power-management-group/power-management/f/power-management-forum/1262246/bq27220evm-744-when-fcc-updated-should-it-be-updated-manually).

The patch passes `git apply --check` against the cited revision and an ARM64
GCC syntax check with `-Werror` against the local prepared kernel headers.
Mock protocol checks covered all three original security states, reserve
compensation, full capacity greater than design capacity, malformed inputs,
busy states, non-default keys, failure after commit, readback mismatch,
rollback, unsupported adapter clocks, and the absence of writes during reads.
The harness also checks immediate `EBUSY` for a contended calibration read.
Run those checks from the application repository with:

```sh
sh data/kernel/tests/run.sh
```

This uses Python 3 and a C++17 host compiler (`CXX` may select one). It extracts
the actual added implementation from the patch into a temporary directory and
compiles it against a simulated I2C device. It does not require a driver checkout,
kernel headers, physical hardware, or privileges, and it deletes its temporary
files on exit. The harness is a protocol/failure simulation, not electrical
validation.

Actual I2C timing, security transitions, reserve handling and rollback still
require board validation before release.
