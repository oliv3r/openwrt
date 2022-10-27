/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _NET_DSA_RTL83XX_H
#define _NET_DSA_RTL83XX_H

#include <linux/bitops.h>
#include <net/dsa.h>
#include "rtl838x.h"


#define RTL8380_VERSION_A 'A'
#define RTL8390_VERSION_A 'A'
#define RTL8380_VERSION_B 'B'

/* RTL838x series */
#define RTL838X_MAC_FORCE_MODE_CTRL_REG(p)              (0xa104 + ((p) * 0x4))
/* Reserved                                                     31 - 28 */
#define RTL838X_MAC_FORCE_MODE_CTRL_PHY_POWER_SEL               BIT(27)
#define RTL838X_MAC_FORCE_MODE_CTRL_RETRY_SPDN_GLITE            BIT(26)
#define RTL838X_MAC_FORCE_MODE_CTRL_EN_MY_SOFTNP                BIT(25)
#define RTL838X_MAC_FORCE_MODE_CTRL_RETRY_SPDN_10M              BIT(24)
#define RTL838X_MAC_FORCE_MODE_CTRL_SPDN_THR                    BIT(23)
#define RTL838X_MAC_FORCE_MODE_CTRL_EN_RETRY_SPD_DN             BIT(22)
#define RTL838X_MAC_FORCE_MODE_CTRL_EN_2PAIR_SPD_DN             BIT(21)
#define RTL838X_MAC_FORCE_MODE_CTRL_BYPASS_GLITE_UP1            BIT(20)
#define RTL838X_MAC_FORCE_MODE_CTRL_GLITE_MASTER_SLV_MANUAL_EN  BIT(19)
#define RTL838X_MAC_FORCE_MODE_CTRL_GLITE_MASTER_SLV_MANUAL_SEL BIT(18)
#define RTL838X_MAC_FORCE_MODE_CTRL_GLITE_PORT_TYPE             BIT(17)
#define RTL838X_MAC_FORCE_MODE_CTRL_GLITE_EEE                   BIT(16)
#define RTL838X_MAC_FORCE_MODE_CTRL_GLITE_SEL                   BIT(15)
#define RTL838X_MAC_FORCE_MODE_CTRL_MEDIA_SEL                   BIT(14)
#define RTL838X_MAC_FORCE_MODE_CTRL_PHY_MASTER_SLV_MANUAL_EN    BIT(13)
#define RTL838X_MAC_FORCE_MODE_CTRL_PHY_MASTER_SLV_MANUAL_SEL   BIT(12)
#define RTL838X_MAC_FORCE_MODE_CTRL_PHY_PORT_TYPE               BIT(11)
#define RTL838X_MAC_FORCE_MODE_CTRL_EEE_1000M_EN                BIT(10)
#define RTL838X_MAC_FORCE_MODE_CTRL_EEE_100M_EN                 BIT(9)
#define RTL838X_MAC_FORCE_MODE_CTRL_FC_EN                       BIT(8)
#define RTL838X_MAC_FORCE_MODE_CTRL_RX_PAUSE_EN                 BIT(7)
#define RTL838X_MAC_FORCE_MODE_CTRL_TX_PAUSE_EN                 BIT(6)
#define RTL838X_MAC_FORCE_MODE_CTRL_SPD_SEL                     GENMASK(5, 4)
#define RTL838X_MAC_FORCE_MODE_CTRL_SPD_SEL_1000M                       0b10
#define RTL838X_MAC_FORCE_MODE_CTRL_SPD_SEL_100M                        0b01
#define RTL838X_MAC_FORCE_MODE_CTRL_SPD_SEL_10M                         0b00
#define RTL838X_MAC_FORCE_MODE_CTRL_DUP_SEL                     BIT(3)
#define RTL838X_MAC_FORCE_MODE_CTRL_NWAY_EN                     BIT(2)
#define RTL838X_MAC_FORCE_MODE_CTRL_LINK_EN                     BIT(1)
#define RTL838X_MAC_FORCE_MODE_CTRL_EN                          BIT(0)

#define RTL838X_MAC_LINK_STS_REG(p)                     (0xa188 + (((p) / 32) * 0x4))
#define _RTL838X_MAC_LINK_STS_MASK                              BIT(0)
#define RTL838X_MAC_LINK_STS_UP                                         0b1
#define RTL838X_MAC_LINK_STS_DOWN                                       0b0
#define RTL838X_MAC_LINK_STS(p, r) \
        (((r) >> ((p) % 32)) & _RTL838X_MAC_LINK_STS_MASK)

#define RTL838X_MAC_LINK_SPD_STS_REG(p)                 (0xa190 + (((p) / 16) * 0x4))
#define _RTL838X_MAC_LINK_SPD_STS_MASK                          GENMASK(1, 0)
#define RTL838X_MAC_LINK_SPD_STS_2G                                     0x3 /* Only for port 24 & 26 */
#define RTL838X_MAC_LINK_SPD_STS_1000M                                  0x2
#define RTL838X_MAC_LINK_SPD_STS_100M                                   0x1
#define RTL838X_MAC_LINK_SPD_STS_10M                                    0x0
#define RTL838X_MAC_LINK_SPD_STS(p, r) \
        (((r) >> (((p) % 16) * 2)) & _RTL838X_MAC_LINK_SPD_STS_MASK)

#define RTL838X_MAC_LINK_DUP_STS_REG(p)                 (0xa19c + (((p) / 32) * 0x4))
#define _RTL838X_MAC_LINK_DUP_STS_MASK                          BIT(0)
#define RTL838X_MAC_LINK_DUP_STS_FULL                                   1
#define RTL838X_MAC_LINK_DUP_STS_HALF                                   0
#define RTL838X_MAC_LINK_DUP_STS(p, r) \
        (((r) >> ((p) % 32)) & _RTL838X_MAC_LINK_DUP_STS_MASK)

#define RTL838X_MAC_TX_PAUSE_STS_REG(p)                 (0xa1a0 + (((p) / 32) * 0x4))
#define _RTL838X_MAC_TX_PAUSE_STS_MASK                          BIT(0)
#define RTL838X_MAC_TX_PAUSE_STS_ON                                     0b1
#define RTL838X_MAC_TX_PAUSE_STS_OFF                                    0b0
#define RTL838X_MAC_TX_PAUSE_STS(p, r) \
        (((r) >> ((p) % 32)) & _RTL838X_MAC_TX_PAUSE_STS_MASK)

#define RTL838X_MAC_RX_PAUSE_STS_REG(p)                 (0xa1a4 + (((p) / 32) * 0x4))
#define _RTL838X_MAC_RX_PAUSE_STS_MASK                          BIT(0)
#define RTL838X_MAC_RX_PAUSE_STS_ON                                     0b1
#define RTL838X_MAC_RX_PAUSE_STS_OFF                                    0b0
#define RTL838X_MAC_RX_PAUSE_STS(p, r) \
        (((r) >> ((p) % 32)) & _RTL838X_MAC_RX_PAUSE_STS_MASK)

#define RTL838X_MAC_PORT_CTRL_REG(p)                    (0xd560 + ((p) * 0x80))
/* Reserved                                                     31 - 15 */
#define RTL838X_MAC_PORT_CTRL_RX_FIFO_ERROR                     BIT(14)
#define RTL838X_MAC_PORT_CTRL_RX_ENTRY_ERROR                    BIT(13)
#define RTL838X_MAC_PORT_CTRL_TX_FIFO_ERROR                     BIT(12)
#define RTL838X_MAC_PORT_CTRL_TX_ENTRY_ERROR                    BIT(11)
#define RTL838X_MAC_PORT_CTRL_RX_RXER_CHK_EN                    BIT(10)
#define RTL838X_MAC_PORT_CTRL_BYP_TX_CRC                        BIT(9)
#define RTL838X_MAC_PORT_CTRL_PASS_ALL_MODE_EN                  BIT(8)
#define RTL838X_MAC_PORT_CTRL_PRECOLLAT_SEL                     GENMASK(7, 6)
#define RTL838X_MAC_PORT_CTRL_LATE_COLI_THR                     GENMASK(5, 4)
#define RTL838X_MAC_PORT_CTRL_RX_CHK_CRC_EN                     BIT(3)
#define RTL838X_MAC_PORT_CTRL_BKPRES_EN                         BIT(2)
#define RTL838X_MAC_PORT_CTRL_TX_EN                             BIT(1)
#define RTL838X_MAC_PORT_CTRL_RX_EN                             BIT(0)
#define RTL838X_MAC_PORT_CTRL_TXRX_EN \
        (RTL838X_MAC_PORT_CTRL_TX_EN | RTL838X_MAC_PORT_CTRL_RX_EN)

/* RTL839x series */
#define RTL839X_MAC_FORCE_MODE_CTRL_REG(p)              (0x02bc + ((p) * 0x4))
/* Reserved                                                     31 - 16 */
#define RTL839X_MAC_FORCE_MODE_CTRL_500M_SPD                    BIT(15)
#define RTL839X_MAC_FORCE_MODE_CTRL_EEEP_1000M_EN               BIT(14)
#define RTL839X_MAC_FORCE_MODE_CTRL_EEEP_500M_EN                BIT(13)
#define RTL839X_MAC_FORCE_MODE_CTRL_EEEP_100M_EN                BIT(12)
#define RTL839X_MAC_FORCE_MODE_CTRL_EEE_10G_EN                  BIT(11)
#define RTL839X_MAC_FORCE_MODE_CTRL_EEE_1000M_EN                BIT(10)
#define RTL839X_MAC_FORCE_MODE_CTRL_EEE_500M_EN                 BIT(9)
#define RTL839X_MAC_FORCE_MODE_CTRL_EEE_100M_EN                 BIT(8)
#define RTL839X_MAC_FORCE_MODE_CTRL_FC_EN                       BIT(7)
#define RTL839X_MAC_FORCE_MODE_CTRL_RX_PAUSE_EN                 BIT(6)
#define RTL839X_MAC_FORCE_MODE_CTRL_TX_PAUSE_EN                 BIT(5)
#define RTL839X_MAC_FORCE_MODE_CTRL_SPD_SEL                     GENMASK(4, 3)
#define RTL839X_MAC_FORCE_MODE_CTRL_SPD_SEL_1000M                       0b10
#define RTL839X_MAC_FORCE_MODE_CTRL_SPD_SEL_100M                        0b01
#define RTL839X_MAC_FORCE_MODE_CTRL_SPD_SEL_10M                         0b00
#define RTL839X_MAC_FORCE_MODE_CTRL_DUP_SEL                     BIT(2)
#define RTL839X_MAC_FORCE_MODE_CTRL_LINK_EN                     BIT(1)
#define RTL839X_MAC_FORCE_MODE_CTRL_EN                          BIT(0)

#define RTL839X_MAC_LINK_STS_REG(p)                     (0x0390 + (((p) / 32) * 0x4))
#define _RTL839X_MAC_LINK_STS_MASK                              BIT(0)
#define RTL839X_MAC_LINK_STS_UP                                         0b1
#define RTL839X_MAC_LINK_STS_DOWN                                       0b0
#define RTL839X_MAC_LINK_STS(p, r) \
        (((r) >> ((p) % 32)) & _RTL839X_MAC_LINK_STS_MASK)

#define RTL839X_MAC_LINK_SPD_STS_REG(p)                 (0x03a0 + (((p) / 16) * 0x4))
#define _RTL839X_MAC_LINK_SPD_STS_MASK                          GENMASK(1, 0)
#define RTL839X_MAC_LINK_SPD_STS_10G                                    0x3
#define RTL839X_MAC_LINK_SPD_STS_500M                                   0x3 /* Only if RTL839X_MAC_LINK_500M_STS */
#define RTL839X_MAC_LINK_SPD_STS_1000M                                  0x2
#define RTL839X_MAC_LINK_SPD_STS_100M                                   0x1
#define RTL839X_MAC_LINK_SPD_STS_10M                                    0x0
#define RTL839X_MAC_LINK_SPD_STS(p, r) \
        (((r) >> (((p) % 16) * 2)) & _RTL839X_MAC_LINK_SPD_STS_MASK)

#define RTL839X_MAC_LINK_DUP_STS_REG(p)                 (0x03b0 + (((p) / 32) * 0x4))
#define _RTL839X_MAC_LINK_DUP_STS_MASK                          BIT(0)
#define RTL839X_MAC_LINK_DUP_STS_FULL                                   0b1
#define RTL839X_MAC_LINK_DUP_STS_HALF                                   0b0
#define RTL839X_MAC_LINK_DUP_STS(p, r) \
        (((r) >> ((p) % 32)) & _RTL839X_MAC_LINK_DUP_STS_MASK)

#define RTL839X_MAC_TX_PAUSE_STS_REG(p)                 (0x03b8 + (((p) / 32) * 0x4))
#define _RTL839X_MAC_TX_PAUSE_STS_MASK                          BIT(0)
#define RTL839X_MAC_TX_PAUSE_STS_ON                                     0b1
#define RTL839X_MAC_TX_PAUSE_STS_OFF                                    0b0
#define RTL839X_MAC_TX_PAUSE_STS(p, r) \
        (((r) >> ((p) % 32)) & _RTL839X_MAC_TX_PAUSE_STS_MASK)

#define RTL839X_MAC_RX_PAUSE_STS_REG(p)                 (0x03c0 + (((p) / 32) * 0x4))
#define _RTL839X_MAC_RX_PAUSE_STS_MASK                          BIT(0)
#define RTL839X_MAC_RX_PAUSE_STS_ON                                     0b1
#define RTL839X_MAC_RX_PAUSE_STS_OFF                                    0b0
#define RTL839X_MAC_RX_PAUSE_STS(p, r) \
        (((r) >> ((p) % 32)) & _RTL839X_MAC_RX_PAUSE_STS_MASK)

#define RTL839X_MAC_LINK_500M_STS_REG(p)                (0x0408 + (((p) / 32) * 0x4))
#define _RTL839X_MAC_LINK_500M_STS_MASK                         BIT(0)
#define RTL839X_MAC_LINK_500M_STS_ON                                    0b1
#define RTL839X_MAC_LINK_500M_STS_OFF                                   0b0
#define RTL839X_MAC_LINK_500M_STS(p, r) \
        (((r) >> ((p) % 32)) & _RTL839X_MAC_LINK_500M_STS_MASK)

#define RTL839X_MAC_PORT_CTRL_REG(p)                    (0x8004 + ((p) * 0x80))
/* Reserved                                                     31 - 29 */
#define RTL839X_MAC_PORT_CTRL_IPG_MIN_RX_SEL                    BIT(28)
#define RTL839X_MAC_PORT_CTRL_IPG_LEN                           GENMASK(27, 8)
#define RTL839X_MAC_PORT_CTRL_BYP_TX_CRC                        BIT(7)
#define RTL839X_MAC_PORT_CTRL_PASS_ALL_MODE_EN                  BIT(6)
#define RTL839X_MAC_PORT_CTRL_LATE_COLI_THR                     GENMASK(5, 4)
#define RTL839X_MAC_PORT_CTRL_RX_CHK_CRC_EN                     BIT(3)
#define RTL839X_MAC_PORT_CTRL_BKPRES_EN                         BIT(2)
#define RTL839X_MAC_PORT_CTRL_TX_EN                             BIT(1)
#define RTL839X_MAC_PORT_CTRL_RX_EN                             BIT(0)
#define RTL839X_MAC_PORT_CTRL_TXRX_EN \
        (RTL839X_MAC_PORT_CTRL_TX_EN | RTL839X_MAC_PORT_CTRL_RX_EN)

/* RTL930x series */
#define RTL930X_MAC_PORT_CTRL_REG(p)                    (0x3260 + ((p) * 0x40))
/* Reserved                                                     31 - 5 */
#define RTL930X_MAC_PORT_CTRL_PRECOLLAT_SEL                     GENMASK(4, 3)
#define RTL930X_MAC_PORT_CTRL_LATE_COLI_THR                     GENMASK(2, 1)
#define RTL930X_MAC_PORT_CTRL_BKPRES_EN                         BIT(0)

#define RTL930X_MAC_L2_PORT_CTRL_REG(p)                 (0x3268 + ((p) * 0x40))
/* Reserved                                                     31 - 6 */
#define RTL930X_MAC_L2_PORT_CTRL_PADDING_UND_SIZE_EN            BIT(5)
#define RTL930X_MAC_L2_PORT_CTRL_RX_CHK_CRC_EN                  BIT(4)
#define RTL930X_MAC_L2_PORT_CTRL_PASS_ALL_MODE_EN               BIT(3)
#define RTL930X_MAC_L2_PORT_CTRL_BYP_TX_CRC                     BIT(2)
#define RTL930X_MAC_L2_PORT_CTRL_TX_EN                          BIT(1)
#define RTL930X_MAC_L2_PORT_CTRL_RX_EN                          BIT(0)
#define RTL930X_MAC_L2_PORT_CTRL_TXRX_EN \
        (RTL930X_MAC_L2_PORT_CTRL_TX_EN | RTL930X_MAC_L2_PORT_CTRL_RX_EN)

#define RTL930X_MAC_FORCE_MODE_CTRL_REG(p)              (0xca1c + ((p) * 0x4))
/* Reserved                                                     31 - 18 */
#define RTL930X_MAC_FORCE_MODE_CTRL_BYP_LINK                    BIT(17)
#define RTL930X_MAC_FORCE_MODE_CTRL_MEDIA_SEL                   BIT(16)
#define RTL930X_MAC_FORCE_MODE_CTRL_EEE_EN_10G                  BIT(15)
#define RTL930X_MAC_FORCE_MODE_CTRL_EEE_EN_5G                   BIT(14)
#define RTL930X_MAC_FORCE_MODE_CTRL_EEE_EN_2G5                  BIT(13)
#define RTL930X_MAC_FORCE_MODE_CTRL_EEE_EN_1000M                BIT(12)
#define RTL930X_MAC_FORCE_MODE_CTRL_EEE_EN_500M                 BIT(11)
#define RTL930X_MAC_FORCE_MODE_CTRL_EEE_EN_100M                 BIT(10)
#define RTL930X_MAC_FORCE_MODE_CTRL_FC_EN                       BIT(9)
#define RTL930X_MAC_FORCE_MODE_CTRL_RX_PAUSE_EN                 BIT(8)
#define RTL930X_MAC_FORCE_MODE_CTRL_TX_PAUSE_EN                 BIT(7)
#define RTL930X_MAC_FORCE_MODE_CTRL_SPD_SEL                     GENMASK(6, 3)
#define RTL930X_MAC_FORCE_MODE_CTRL_SPD_SEL_5G                          0x6
#define RTL930X_MAC_FORCE_MODE_CTRL_SPD_SEL_2G5                         0x5
#define RTL930X_MAC_FORCE_MODE_CTRL_SPD_SEL_10G                         0x4
#define RTL930X_MAC_FORCE_MODE_CTRL_SPD_SEL_500M                        0x3
#define RTL930X_MAC_FORCE_MODE_CTRL_SPD_SEL_1000M                       0x2
#define RTL930X_MAC_FORCE_MODE_CTRL_SPD_SEL_100M                        0x1
#define RTL930X_MAC_FORCE_MODE_CTRL_SPD_SEL_10M                         0x0
#define RTL930X_MAC_FORCE_MODE_CTRL_DUP_SEL                     BIT(2)
#define RTL930X_MAC_FORCE_MODE_CTRL_LINK_EN                     BIT(1)
#define RTL930X_MAC_FORCE_MODE_CTRL_EN                          BIT(0)

#define RTL930X_MAC_LINK_STS_REG(p)                     (0xcb10 + (((p) / 32) * 0x4))
#define RTL930X_MAC_LINK_STS_MASK                               BIT(0)
#define RTL930X_MAC_LINK_STS_UP                                         0b1
#define RTL930X_MAC_LINK_STS_DOWN                                       0b0
#define RTL930X_MAC_LINK_STS(p, r) \
        (((r) >> ((p) % 32)) & RTL930X_MAC_LINK_STS_MASK)

#define RTL930X_MAC_LINK_SPD_STS_REG(p)                 (0xcb18 + (((p) / 8) * 0x4))
#define _RTL930X_MAC_LINK_SPD_STS_MASK                          GENMASK(3, 0)
#define RTL930X_MAC_LINK_SPD_STS_2G5_ALT                                0x8
#define RTL930X_MAC_LINK_SPD_STS_1000M_ALT                              0x7
#define RTL930X_MAC_LINK_SPD_STS_5G                                     0x6
#define RTL930X_MAC_LINK_SPD_STS_2G5                                    0x5
#define RTL930X_MAC_LINK_SPD_STS_10G                                    0x4
#define RTL930X_MAC_LINK_SPD_STS_500M                                   0x3
#define RTL930X_MAC_LINK_SPD_STS_1000M                                  0x2
#define RTL930X_MAC_LINK_SPD_STS_100M                                   0x1
#define RTL930X_MAC_LINK_SPD_STS_10M                                    0x0
#define RTL930X_MAC_LINK_SPD_STS(p, r) \
        (((r) >> (((p) % 8) * 4)) & _RTL930X_MAC_LINK_SPD_STS_MASK)

#define RTL930X_MAC_LINK_DUP_STS_REG(p)                 (0xcb28 + (((p) / 32) * 0x4))
#define _RTL930X_MAC_LINK_DUP_STS_MASK                          BIT(0)
#define RTL930X_MAC_LINK_DUP_STS_FULL                                   0b1
#define RTL930X_MAC_LINK_DUP_STS_HALF                                   0b0
#define RTL930X_MAC_LINK_DUP_STS(p, r) \
        (((r) >> ((p) % 32)) & _RTL930X_MAC_LINK_DUP_STS_MASK)

#define RTL930X_MAC_TX_PAUSE_STS_REG(p)                 (0xcb2c + (((p) / 32) * 0x4))
#define _RTL930X_MAC_TX_PAUSE_STS_MASK                          BIT(0)
#define RTL930X_MAC_TX_PAUSE_STS_ON                                     0b1
#define RTL930X_MAC_TX_PAUSE_STS_OFF                                    0b0
#define RTL930X_MAC_TX_PAUSE_STS(p, r) \
        (((r) >> ((p) % 32)) & _RTL930X_MAC_TX_PAUSE_STS_MASK)

#define RTL930X_MAC_RX_PAUSE_STS_REG(p)                 (0xcb30 + (((p) / 32) * 0x4))
#define _RTL930X_MAC_RX_PAUSE_STS_MASK                          BIT(0)
#define RTL930X_MAC_RX_PAUSE_STS_ON                                     0b1
#define RTL930X_MAC_RX_PAUSE_STS_OFF                                    0b0
#define RTL930X_MAC_RX_PAUSE_STS(p, r) \
        (((r) >> ((p) % 32)) & _RTL930X_MAC_RX_PAUSE_STS_MASK)

#define RTL930X_MAC_LINK_MEDIA_STS_REG(p)               (0xcb14 + (((p) / 32) * 0x4))
#define _RTL930X_MAC_LINK_MEDIA_STS_MASK                        BIT(0)
#define RTL930X_MAC_LINK_MEDIA_STS_FIBER                                0b1
#define RTL930X_MAC_LINK_MEDIA_STS_COPPER                               0b0
#define RTL930X_MAC_LINK_MEDIA_STS(p, r) \
        (((r) >> ((p) % 32)) & _RTL930X_MAC_LINK_MEDIA_STS_MASK)

/* RTL931x series */
#define RTL931X_MAC_FORCE_MODE_CTRL_REG(p)              (0x0dcc + ((p) * 0x4))
/* Reserved                                                     31 - 28 */
#define RTL931X_MAC_FORCE_MODE_CTRL_SDS_ABLTY                   BIT(27)
/* Reserved                                                     26 */
#define RTL931X_MAC_FORCE_MODE_CTRL_FEFI_SEL                    BIT(25)
#define RTL931X_MAC_FORCE_MODE_CTRL_MSTR_SLV                    BIT(24)
#define RTL931X_MAC_FORCE_MODE_CTRL_EEE_10G_EN                  BIT(23)
#define RTL931X_MAC_FORCE_MODE_CTRL_EEE_5G_EN                   BIT(22)
#define RTL931X_MAC_FORCE_MODE_CTRL_EEE_2G5_EN                  BIT(21)
#define RTL931X_MAC_FORCE_MODE_CTRL_EEE_1000M_EN                BIT(20)
#define RTL931X_MAC_FORCE_MODE_CTRL_EEE_500M_EN                 BIT(19)
#define RTL931X_MAC_FORCE_MODE_CTRL_EEE_100M_EN                 BIT(18)
#define RTL931X_MAC_FORCE_MODE_CTRL_RX_PAUSE_EN                 BIT(17)
#define RTL931X_MAC_FORCE_MODE_CTRL_TX_PAUSE_EN                 BIT(16)
#define RTL931X_MAC_FORCE_MODE_CTRL_SPD_SEL                     GENMASK(15, 12)
#define RTL931X_MAC_FORCE_MODE_CTRL_SPD_SEL_5G                          0x6
#define RTL931X_MAC_FORCE_MODE_CTRL_SPD_SEL_2G5                         0x5
#define RTL931X_MAC_FORCE_MODE_CTRL_SPD_SEL_10G                         0x4
#define RTL931X_MAC_FORCE_MODE_CTRL_SPD_SEL_500M                        0x3
#define RTL931X_MAC_FORCE_MODE_CTRL_SPD_SEL_1000M                       0x2
#define RTL931X_MAC_FORCE_MODE_CTRL_SPD_SEL_100M                        0x1
#define RTL931X_MAC_FORCE_MODE_CTRL_SPD_SEL_10M                         0x0
#define RTL931X_MAC_FORCE_MODE_CTRL_DUP_SEL                     BIT(11)
#define RTL931X_MAC_FORCE_MODE_CTRL_MEDIA                       BIT(10)
#define RTL931X_MAC_FORCE_MODE_CTRL_EN                          BIT(9)
/* Reserved                                                     8 */
#define RTL931X_MAC_FORCE_MODE_CTRL_FEFI_EN                     BIT(7)
#define RTL931X_MAC_FORCE_MODE_CTRL_MSTR_SLV_EN                 BIT(6)
#define RTL931X_MAC_FORCE_MODE_CTRL_EEE_EN                      BIT(5)
#define RTL931X_MAC_FORCE_MODE_CTRL_FC_EN                       BIT(4)
#define RTL931X_MAC_FORCE_MODE_CTRL_SPD_EN                      BIT(3)
#define RTL931X_MAC_FORCE_MODE_CTRL_DUP_EN                      BIT(2)
#define RTL931X_MAC_FORCE_MODE_CTRL_MEDIA_EN                    BIT(1)
#define RTL931X_MAC_FORCE_MODE_CTRL_LINK_EN                     BIT(0)

#define RTL931X_MAC_LINK_STS_REG(p)                     (0x0ec0 + (((p) / 32) * 0x4))
#define RTL931X_MAC_LINK_STS_MASK                               BIT(0)
#define RTL931X_MAC_LINK_STS_UP                                         0b1
#define RTL931X_MAC_LINK_STS_DOWN                                       0b0
#define RTL931X_MAC_LINK_STS(p, r) \
        (((r) >> ((p) % 32)) & RTL931X_MAC_LINK_STS_MASK)

#define RTL931X_MAC_LINK_SPD_STS_REG(p)                 (0x0ed0 + (((p) / 8) * 0x4))
#define _RTL931X_MAC_LINK_SPD_STS_MASK                          GENMASK(3, 0)
#define RTL931X_MAC_LINK_SPD_STS_2G5_ALT                                0x8
#define RTL931X_MAC_LINK_SPD_STS_1000M_ALT                              0x7
#define RTL931X_MAC_LINK_SPD_STS_5G                                     0x6
#define RTL931X_MAC_LINK_SPD_STS_2G5                                    0x5
#define RTL931X_MAC_LINK_SPD_STS_10G                                    0x4
#define RTL931X_MAC_LINK_SPD_STS_500M                                   0x3
#define RTL931X_MAC_LINK_SPD_STS_1000M                                  0x2
#define RTL931X_MAC_LINK_SPD_STS_100M                                   0x1
#define RTL931X_MAC_LINK_SPD_STS_10M                                    0x0
#define RTL931X_MAC_LINK_SPD_STS(p, r) \
        (((r) >> (((p) % 8) * 4)) & _RTL931X_MAC_LINK_SPD_STS_MASK)

#define RTL931X_MAC_LINK_MEDIA_STS_REG(p)               (0x0ec8 + (((p) / 32) * 0x4))
#define _RTL931X_MAC_LINK_MEDIA_STS_MASK                        BIT(0)
#define RTL931X_MAC_LINK_MEDIA_STS_FIBER                                0b1
#define RTL931X_MAC_LINK_MEDIA_STS_COPPER                               0b0
#define RTL931X_MAC_LINK_MEDIA_STS(p, r) \
        (((r) >> ((p) % 32)) & _RTL931X_MAC_LINK_MEDIA_STS_MASK)

#define RTL931X_MAC_LINK_DUP_STS_REG(p)                 (0x0ef0 + (((p) / 32) * 0x4))
#define _RTL931X_MAC_LINK_DUP_STS_MASK                          BIT(0)
#define RTL931X_MAC_LINK_DUP_STS_FULL                                   0b1
#define RTL931X_MAC_LINK_DUP_STS_HALF                                   0b0
#define RTL931X_MAC_LINK_DUP_STS(p, r) \
        (((r) >> ((p) % 32)) & _RTL931X_MAC_LINK_DUP_STS_MASK)

#define RTL931X_MAC_TX_PAUSE_STS_REG(p)                 (0x0ef8 + (((p) / 32) * 0x4))
#define _RTL931X_MAC_TX_PAUSE_STS_MASK                          BIT(0)
#define RTL931X_MAC_TX_PAUSE_STS_ON                                     0b1
#define RTL931X_MAC_TX_PAUSE_STS_OFF                                    0b0
#define RTL931X_MAC_TX_PAUSE_STS(p, r) \
        (((r) >> ((p) % 32)) & _RTL931X_MAC_TX_PAUSE_STS_MASK)

#define RTL931X_MAC_RX_PAUSE_STS_REG(p)                 (0x0f00 + (((p) / 32) * 0x4))
#define _RTL931X_MAC_RX_PAUSE_STS_MASK                          BIT(0)
#define RTL931X_MAC_RX_PAUSE_STS_ON                                     0b1
#define RTL931X_MAC_RX_PAUSE_STS_OFF                                    0b0
#define RTL931X_MAC_RX_PAUSE_STS(p, r) \
        (((r) >> ((p) % 32)) & _RTL931X_MAC_RX_PAUSE_STS_MASK)

#define RTL931X_MAC_L2_PORT_CTRL_REG(p)                 (0x6000 + ((p) * 0x80))
/* Reserved                                                     31 - 21 */
#define RTL931X_MAC_L2_PORT_CTRL_PER_PORT_MAC_ECO               BIT(22)
#define RTL931X_MAC_L2_PORT_CTRL_STK_1G_PKT_FMT                 BIT(21)
#define RTL931X_MAC_L2_PORT_CTRL_TX_IPG                         GENMASK(20, 5)
#define RTL931X_MAC_L2_PORT_CTRL_PADDING_UND_SIZE_EN            BIT(4)
#define RTL931X_MAC_L2_PORT_CTRL_PASS_ALL_MODE_EN               BIT(3)
#define RTL931X_MAC_L2_PORT_CTRL_BYP_TX_CRC                     BIT(2)
#define RTL931X_MAC_L2_PORT_CTRL_TX_EN                          BIT(1)
#define RTL931X_MAC_L2_PORT_CTRL_RX_EN                          BIT(0)
#define RTL931X_MAC_L2_PORT_CTRL_TXRX_EN \
        (RTL931X_MAC_L2_PORT_CTRL_TX_EN | RTL931X_MAC_L2_PORT_CTRL_RX_EN)

#define RTL931X_MAC_PORT_CTRL_REG(p)                    (0x6004 + ((p) * 0x80))
/* Reserved                                                     31 - 5 */
#define RTL931X_MAC_PORT_CTRL_PRECOLLAT_SEL                     GENMASK(4, 3)
#define RTL931X_MAC_PORT_CTRL_LATE_COLI_THR                     GENMASK(2 ,1)
#define RTL931X_MAC_PORT_CTRL_BKPRES_EN                         BIT(0)

#define MIB_DESC(_size, _offset, _name) {.size = _size, .offset = _offset, .name = _name}
struct rtl83xx_mib_desc {
	unsigned int size;
	unsigned int offset;
	const char *name;
};

/* API for switch table access */
struct table_reg {
	u16 addr;
	u16 data;
	u8  max_data;
	u8 c_bit;
	u8 t_bit;
	u8 rmode;
	u8 tbl;
	struct mutex lock;
};

#define TBL_DESC(_addr, _data, _max_data, _c_bit, _t_bit, _rmode) \
		{  .addr = _addr, .data = _data, .max_data = _max_data, .c_bit = _c_bit, \
		    .t_bit = _t_bit, .rmode = _rmode \
		}

typedef enum {
	RTL8380_TBL_L2 = 0,
	RTL8380_TBL_0,
	RTL8380_TBL_1,
	RTL8390_TBL_L2,
	RTL8390_TBL_0,
	RTL8390_TBL_1,
	RTL8390_TBL_2,
	RTL9300_TBL_L2,
	RTL9300_TBL_0,
	RTL9300_TBL_1,
	RTL9300_TBL_2,
	RTL9300_TBL_HSB,
	RTL9300_TBL_HSA,
	RTL9310_TBL_0,
	RTL9310_TBL_1,
	RTL9310_TBL_2,
	RTL9310_TBL_3,
	RTL9310_TBL_4,
	RTL9310_TBL_5,
	RTL_TBL_END
} rtl838x_tbl_reg_t;

void rtl_table_init(void);
struct table_reg *rtl_table_get(rtl838x_tbl_reg_t r, int t);
void rtl_table_release(struct table_reg *r);
void rtl_table_read(struct table_reg *r, int idx);
void rtl_table_write(struct table_reg *r, int idx);
inline u16 rtl_table_data(struct table_reg *r, int i);
inline u32 rtl_table_data_r(struct table_reg *r, int i);
inline void rtl_table_data_w(struct table_reg *r, u32 v, int i);

void __init rtl83xx_setup_qos(struct rtl838x_switch_priv *priv);

int rtl83xx_packet_cntr_alloc(struct rtl838x_switch_priv *priv);

int rtl83xx_port_is_under(const struct net_device * dev, struct rtl838x_switch_priv *priv);

int read_phy(u32 port, u32 page, u32 reg, u32 *val);
int write_phy(u32 port, u32 page, u32 reg, u32 val);

/* Port register accessor functions for the RTL839x and RTL931X SoCs */
void rtl839x_mask_port_reg_be(u64 clear, u64 set, int reg);
u64 rtl839x_get_port_reg_be(int reg);
void rtl839x_set_port_reg_be(u64 set, int reg);
void rtl839x_mask_port_reg_le(u64 clear, u64 set, int reg);
void rtl839x_set_port_reg_le(u64 set, int reg);
u64 rtl839x_get_port_reg_le(int reg);

/* Port register accessor functions for the RTL838x and RTL930X SoCs */
void rtl838x_mask_port_reg(u64 clear, u64 set, int reg);
void rtl838x_set_port_reg(u64 set, int reg);
u64 rtl838x_get_port_reg(int reg);

/* RTL838x-specific */
u32 rtl838x_hash(struct rtl838x_switch_priv *priv, u64 seed);
irqreturn_t rtl838x_switch_irq(int irq, void *dev_id);
void rtl8380_get_version(struct rtl838x_switch_priv *priv);
void rtl838x_vlan_profile_dump(int index);
int rtl83xx_dsa_phy_read(struct dsa_switch *ds, int phy_addr, int phy_reg);
void rtl8380_sds_rst(int mac);
int rtl8380_sds_power(int mac, int val);
void rtl838x_print_matrix(void);

/* RTL839x-specific */
u32 rtl839x_hash(struct rtl838x_switch_priv *priv, u64 seed);
irqreturn_t rtl839x_switch_irq(int irq, void *dev_id);
void rtl8390_get_version(struct rtl838x_switch_priv *priv);
void rtl839x_vlan_profile_dump(int index);
int rtl83xx_dsa_phy_write(struct dsa_switch *ds, int phy_addr, int phy_reg, u16 val);
void rtl839x_exec_tbl2_cmd(u32 cmd);
void rtl839x_print_matrix(void);

/* RTL930x-specific */
u32 rtl930x_hash(struct rtl838x_switch_priv *priv, u64 seed);
irqreturn_t rtl930x_switch_irq(int irq, void *dev_id);
irqreturn_t rtl839x_switch_irq(int irq, void *dev_id);
void rtl930x_vlan_profile_dump(int index);
int rtl9300_sds_power(int mac, int val);
void rtl9300_sds_rst(int sds_num, u32 mode);
int rtl9300_serdes_setup(int sds_num, phy_interface_t phy_mode);
void rtl930x_print_matrix(void);

/* RTL931x-specific */
irqreturn_t rtl931x_switch_irq(int irq, void *dev_id);
int rtl931x_sds_cmu_band_get(int sds, phy_interface_t mode);
int rtl931x_sds_cmu_band_set(int sds, bool enable, u32 band, phy_interface_t mode);
void rtl931x_sds_init(u32 sds, phy_interface_t mode);

int rtl83xx_lag_add(struct dsa_switch *ds, int group, int port, struct netdev_lag_upper_info *info);
int rtl83xx_lag_del(struct dsa_switch *ds, int group, int port);

#endif /* _NET_DSA_RTL83XX_H */
