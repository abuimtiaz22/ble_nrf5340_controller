# read_all.py
import asyncio
from bleak import BleakClient

# ==== EDIT THESE TO MATCH YOUR BOARD ====
DEVICE_ADDRESS   = "F3:4C:6C:A2:BD:C7"   # your board's MAC
UUID_FFF1_WRITE  = "0000fff1-0000-1000-8000-00805f9b34fb"  # command write
UUID_FFF2_NOTIFY = "0000fff2-0000-1000-8000-00805f9b34fb"  # DONE_xx notify
CSV_CHAR_HANDLE  = 28  # 0x2B29 in your app service (from discover.py)

TOTAL_READS = 128
NUM_PINS    = 14

async def run_loop_1_to_3(client: BleakClient, loop_id: int):
    msg = f"{loop_id:02d}".encode()          # b"01"/b"02"/b"03"
    done_token = f"DONE_{loop_id:02d}".encode()
    done_event = asyncio.Event()

    def _notify(_, data: bytearray):
        try:
            if done_token in bytes(data):
                print(f"[NOTIFY] {bytes(data)!r}")
                done_event.set()
        except Exception:
            pass

    # Try to subscribe to DONE notifications; keep going if not supported
    subscribed = False
    try:
        await client.start_notify(UUID_FFF2_NOTIFY, _notify)
        subscribed = True
    except Exception as e:
        print(f"(warn) Could not subscribe to notifications: {e}")

    print(f"Sending loop {loop_id:02d} command...")
    await client.write_gatt_char(UUID_FFF1_WRITE, msg, response=True)

    if subscribed:
        try:
            await asyncio.wait_for(done_event.wait(), timeout=10.0)
            print(f"Loop {loop_id:02d} reported DONE.")
        except asyncio.TimeoutError:
            print(f"(warn) No DONE notification within 10s (command was sent).")
        finally:
            try:
                await client.stop_notify(UUID_FFF2_NOTIFY)
            except Exception:
                pass
    else:
        await asyncio.sleep(1.0)
        print(f"Loop {loop_id:02d} command sent (no notify subscription).")

async def run_loop_4_and_read(client: BleakClient):
    done_event = asyncio.Event()

    def _notify(_, data: bytearray):
        msg = data.decode(errors="ignore").strip()
        if "DONE_04" in msg:
            print(f"[NOTIFY] {msg}")
            done_event.set()

    # 1) subscribe BEFORE triggering to avoid race
    await client.start_notify(UUID_FFF2_NOTIFY, _notify)

    # 2) trigger Loop-4 via the command characteristic (your firmware path that notifies DONE_04)
    await client.write_gatt_char(UUID_FFF1_WRITE, b"04", response=True)

    print("Loop 4 armed. Reading 128 CSV rows...")

    all_values = []
    for i in range(TOTAL_READS):
        raw = await client.read_gatt_char(CSV_CHAR_HANDLE)   # your FFF3 read|write char (0x2B29 handle 28)
        txt = raw.decode(errors="ignore").strip()
        parts = [p.strip() for p in txt.split(",") if p.strip() != ""]
        if len(parts) != NUM_PINS:
            print(f"Row {i}: expected {NUM_PINS}, got {len(parts)} -> {txt!r}")
            break
        all_values.extend(parts)

    # 3) wait for DONE (don’t block earlier; this avoids ordering races)
    try:
        await asyncio.wait_for(done_event.wait(), timeout=10.0)
    except asyncio.TimeoutError:
        print("(warn) DONE_04 not received within 10s (rows were read).")
    finally:
        try:
            await client.stop_notify(UUID_FFF2_NOTIFY)
        except Exception:
            pass

    # Pack pin0 = LSB into integer per row, then save
    lines = []
    for i in range(TOTAL_READS):
        bits = [int(b) for b in all_values[i*NUM_PINS:(i+1)*NUM_PINS]]
        v = 0
        for bit_idx, bit in enumerate(bits):
            v |= (bit & 1) << bit_idx
        lines.append(str(v))

    with open("gpio_binary_values.txt", "w") as f:
        f.write("\n".join(lines))
    print("Saved gpio_binary_values.txt")


async def menu_loop():
    async with BleakClient(DEVICE_ADDRESS) as client:
        if not client.is_connected:
            print("Connection failed.")
            return

        while True:
            print("\nSelect loop to run:")
            print("  1) Loop 1 (RST/ENBIAS/STAB sequence)")
            print("  2) Loop 2 (96-bit CLK/DATA stream)")
            print("  3) Loop 3 (pulse/rst toggling)")
            print("  4) Loop 4 (14x128 capture to file)")
            print("  q) Quit")
            choice = input("Enter 1, 2, 3, 4, or q: ").strip().lower()

            if choice in {"q", "x", "exit"}:
                print("Exiting.")
                break
            elif choice in {"1", "2", "3"}:
                await run_loop_1_to_3(client, int(choice))
            elif choice == "4":
                await run_loop_4_and_read(client)
            else:
                print("Invalid choice. Try again.")

if __name__ == "__main__":
    asyncio.run(menu_loop())
