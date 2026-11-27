#!/usr/bin/env python3
"""mdio-compat.py [--genphy <phy_device.c>] <file ...>: turn on Synology's
own port of these drivers to the 4.5 mdio_device phylib.

Synology ported tg3, b44, the Broadcom/Realtek/Microchip PHY drivers,
lan78xx and ax88172a to the newer phylib for its Marvell Armada kernels, under
CONFIG_SYNO_LSP_ARMADA_16_12. r1000 and v1000 carry the same phylib under
CONFIG_SYNO_AMD_XGBE_PORTING (include/linux/phy.h tests either option), but
the drivers only test the Armada one. Each such test gets the AMD option
too, exactly as phy.h has it.

A driver Synology did not port (in the 7.2/7.3 sources) gets
the same changes upstream 4.5 made to it (mdio_layout()).

With --genphy <phy_device.c>: the same port also moved genphy_config_init()'s
EXPORT_SYMBOL into a branch the AMD option leaves out, so vmlinux has the
function but does not export it; the drivers that use it get a private copy
of the kernel's own (it only reads BMSR/ESTATUS into phydev->supported)."""
import re
import sys

ARMADA = 'CONFIG_SYNO_LSP_ARMADA_16_12'
BOTH = '(defined(CONFIG_SYNO_LSP_ARMADA_16_12) || defined(CONFIG_SYNO_AMD_XGBE_PORTING))'
NL = '\n'

args = sys.argv[1:]
genphy = None
if args[:1] == ['--genphy']:
    src = open(args[1]).read()
    m = re.search(r'^int genphy_config_init\(struct phy_device \*phydev\)$.*?^\}$', src, re.S | re.M)
    body = m.group(0).replace('int genphy_config_init(', 'int arc_genphy_config_init(', 1)
    genphy = NL.join(['',
                      '/* arc-modules: vmlinux does not export genphy_config_init() here */',
                      '#define genphy_config_init arc_genphy_config_init',
                      'static ' + body, ''])
    args = args[2:]

PHY = r'(phydev|phy_dev)'


def mdio_layout(t):
    """What Synology's port does, for a driver it did not port (tg3 and b44
    in the 7.2/7.3 sources): the changes upstream 4.5 made to them."""
    # the bus' PHY list is mdio_map now: through mdiobus_get_phy()
    t = re.sub(r'([A-Za-z_]\w*(?:->\w+)*)->phy_map\[([^\]]+)\]', r'mdiobus_get_phy(\1, \2)', t)
    # address and device of a PHY live in its mdio_device
    t = re.sub(r'dev_name\(&' + PHY + r'->dev\)', r'phydev_name(\1)', t)
    t = re.sub(r'\b' + PHY + r'->addr\b', r'\1->mdio.addr', t)
    t = re.sub(r'&' + PHY + r'->dev\b', r'&\1->mdio.dev', t)
    # the indirect MMD accessors lost their address argument (it is the PHY's)
    t = re.sub(r'(phy_(?:read|write)_mmd_indirect\([^;]*?),\s*' + PHY + r'->mdio\.addr\b', r'\1', t)
    # phy_driver has no .driver.owner (module_phy_driver passes THIS_MODULE)
    t = re.sub(r'\n[ \t]*\.driver[ \t]*=[ \t]*\{[ \t]*\.owner[ \t]*=[ \t]*THIS_MODULE[ \t]*,?[ \t]*\}[ \t]*,?', '', t)
    # mii_bus->irq is an array inside the bus now, already PHY_POLL
    t = re.sub(r'\n[^\n]*\b\w*(?:bus|mdio)->irq[ \t]*=[^;\n]*;', '', t)
    t = re.sub(r'\n[^\n]*kfree\([^)\n]*(?:bus|mdio)->irq\);', '', t)
    return t


for f in args:
    t = open(f).read()
    o = t
    if ARMADA not in t:
        t = mdio_layout(t)
    if genphy and f.endswith('.c') and re.search(r'\bgenphy_config_init\b', t):
        lines = t.split(NL)
        last = max(n for n, line in enumerate(lines) if line.startswith('#include'))
        lines.insert(last + 1, genphy)
        t = NL.join(lines)
    t = re.sub(r'^#ifdef[ \t]+' + ARMADA + r'\b', '#if ' + BOTH, t, flags=re.M)
    t = re.sub(r'^#ifndef[ \t]+' + ARMADA + r'\b', '#if !' + BOTH, t, flags=re.M)
    t = re.sub(r'(?<![|(] )defined\(' + ARMADA + r'\)(?! \|\| defined\(CONFIG_SYNO_AMD_XGBE_PORTING\))', BOTH, t)
    if t != o:
        open(f, 'w').write(t)
        print(f)
