#!/usr/bin/python3
"""Real BlueZ D-Bus GATT test; uses distro python3-dbus/python3-gi.

Run `ble start` on the board. Only the specified board is connected.
Compatible with the test machine's BlueZ 5.53; no pip dependency required.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import queue
import threading
import time

import dbus
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib

SERVICE = 'ab320001-5e7f-4c9a-9b7b-75ba2a2f0001'
VALUE = 'ab320001-5e7f-4c9a-9b7b-75ba2a2f0002'


def wait_for(fn, seconds=15):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        value = fn()
        if value:
            return value
        time.sleep(0.05)
    raise TimeoutError('Bluetooth operation timed out')


def run(args):
    DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus()
    loop = GLib.MainLoop()
    thread = threading.Thread(target=loop.run, daemon=True)
    thread.start()
    manager = dbus.Interface(bus.get_object('org.bluez', '/'), 'org.freedesktop.DBus.ObjectManager')
    adapter = dbus.Interface(bus.get_object('org.bluez', '/org/bluez/' + args.adapter), 'org.bluez.Adapter1')
    path = '/org/bluez/' + args.adapter + '/dev_' + args.address.upper().replace(':', '_')
    results = []
    try:
        adapter.SetDiscoveryFilter(dbus.Dictionary({'Transport': 'le'}, signature='sv'))
        adapter.StartDiscovery()
        try:
            wait_for(lambda: path in manager.GetManagedObjects())
        finally:
            adapter.StopDiscovery()
        dev_obj = bus.get_object('org.bluez', path)
        dev = dbus.Interface(dev_obj, 'org.bluez.Device1')
        props = dbus.Interface(dev_obj, 'org.freedesktop.DBus.Properties')
        for cycle in range(args.rounds):
            listener = None
            try:
                dev.Connect(timeout=20)
                wait_for(lambda: props.Get('org.bluez.Device1', 'ServicesResolved'), 20)
                objects = manager.GetManagedObjects()
                services = [p for p, v in objects.items() if str(v.get('org.bluez.GattService1', {}).get('UUID', '')) == SERVICE and p.startswith(path + '/')]
                assert len(services) == 1, 'Test service missing'
                chars = [p for p, v in objects.items() if str(v.get('org.bluez.GattCharacteristic1', {}).get('UUID', '')) == VALUE and p.startswith(services[0] + '/')]
                assert len(chars) == 1, 'Test characteristic missing'
                char_path = str(chars[0])
                char = dbus.Interface(bus.get_object('org.bluez', char_path), 'org.bluez.GattCharacteristic1')
                notifications = queue.Queue()
                def changed(interface, values, invalid):
                    if interface == 'org.bluez.GattCharacteristic1' and 'Value' in values:
                        notifications.put(bytes(values['Value']))
                listener = bus.add_signal_receiver(changed, signal_name='PropertiesChanged', dbus_interface='org.freedesktop.DBus.Properties', path=char_path)
                options = dbus.Dictionary({}, signature='sv')
                initial = bytes(char.ReadValue(options, timeout=10))
                # ReadValue can emit Value too; start with an empty signal queue.
                time.sleep(0.1)
                while not notifications.empty():
                    notifications.get_nowait()
                char.StartNotify(timeout=10)
                sizes = []
                for size in (1, 20, 60):
                    payload = bytes((cycle + i) & 255 for i in range(size))
                    char.WriteValue(dbus.ByteArray(payload), dbus.Dictionary({'type': 'request'}, signature='sv'), timeout=10)
                    notified = notifications.get(timeout=10)
                    assert notified == payload, (len(notified), size)
                    char.StopNotify(timeout=10)
                    actual = bytes(char.ReadValue(options, timeout=10))
                    assert actual == payload
                    time.sleep(0.1)
                    while not notifications.empty():
                        notifications.get_nowait()
                    char.StartNotify(timeout=10)
                    sizes.append(size)
                char.StopNotify(timeout=10)
                results.append(dict(cycle=cycle + 1, initial_hex=initial.hex(), read_write_notify_sizes=sizes))
            finally:
                if listener:
                    listener.remove()
                dev.Disconnect(timeout=10)
            print(f'PASS cycle {cycle + 1}: connect, discovery, read/write/notify {sizes}, disconnect', flush=True)
            time.sleep(1)
    finally:
        loop.quit()
        thread.join(timeout=2)
    report = dict(recorded_at=datetime.now(timezone.utc).isoformat(), address=args.address,
                  service_uuid=SERVICE, characteristic_uuid=VALUE, cycles=results)
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--address', default='C2:00:AB:32:00:02')
    p.add_argument('--adapter', default='hci0')
    p.add_argument('--rounds', type=int, default=3)
    p.add_argument('--output', type=Path)
    args = p.parse_args()
    if not 1 <= args.rounds <= 100:
        p.error('--rounds must be 1..100')
    run(args)
