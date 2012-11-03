/*
 * QEMU Geforce NV2A implementation
 *
 * Copyright (c) 2012 espes
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */
#include "hw.h"
#include "pc.h"
#include "console.h"
#include "pci.h"
#include "vga.h"
#include "vga_int.h"

#include "nv2a.h"

//#define DEBUG_NV2A
#ifdef DEBUG_NV2A
# define NV2A_DPRINTF(format, ...)       printf(format, ## __VA_ARGS__)
#else
# define NV2A_DPRINTF(format, ...)       do { } while (0)
#endif


#define NV_NUM_BLOCKS 21
#define NV_PMC          0   /* card master control */
#define NV_PBUS         1   /* bus control */
#define NV_PFIFO        2   /* MMIO and DMA FIFO submission to PGRAPH and VPE */
#define NV_PFIFO_CACHE  3
#define NV_PRMA         4   /* access to BAR0/BAR1 from real mode */
#define NV_PVIDEO       5   /* video overlay */
#define NV_PTIMER       6   /* time measurement and time-based alarms */
#define NV_PCOUNTER     7   /* performance monitoring counters */
#define NV_PVPE         8   /* MPEG2 decoding engine */
#define NV_PTV          9   /* TV encoder */
#define NV_PRMFB        10  /* aliases VGA memory window */
#define NV_PRMVIO       11  /* aliases VGA sequencer and graphics controller registers */
#define NV_PFB          12  /* memory interface */
#define NV_PSTRAPS      13  /* straps readout / override */
#define NV_PGRAPH       14  /* accelerated 2d/3d drawing engine */
#define NV_PCRTC        15  /* more CRTC controls */
#define NV_PRMCIO       16  /* aliases VGA CRTC and attribute controller registers */
#define NV_PRAMDAC      17  /* RAMDAC, cursor, and PLL control */
#define NV_PRMDIO       18  /* aliases VGA palette registers */
#define NV_PRAMIN       19  /* RAMIN access */
#define NV_USER         20  /* PFIFO MMIO and DMA submission area */



#define NV_PMC_BOOT_0                                    0x00000000
#define NV_PMC_INTR_0                                    0x00000100
#   define NV_PMC_INTR_0_PFIFO                                 (1 << 8)
#   define NV_PMC_INTR_0_PGRAPH                               (1 << 12)
#   define NV_PMC_INTR_0_PCRTC                                (1 << 24)
#   define NV_PMC_INTR_0_PBUS                                 (1 << 28)
#   define NV_PMC_INTR_0_SOFTWARE                             (1 << 31)
#define NV_PMC_INTR_EN_0                                 0x00000140
#   define NV_PMC_INTR_EN_0_HARDWARE                            1
#   define NV_PMC_INTR_EN_0_SOFTWARE                            2
#define NV_PMC_ENABLE                                    0x00000200
#   define NV_PMC_ENABLE_PFIFO                                 (1 << 8)
#   define NV_PMC_ENABLE_PGRAPH                               (1 << 12)


/* These map approximately to the pci registers */
#define NV_PBUS_PCI_NV_0                                 0x00000800
#   define NV_PBUS_PCI_NV_0_VENDOR_ID                         0x0000FFFF
#   define NV_CONFIG_PCI_NV_0_DEVICE_ID                       0xFFFF0000
#define NV_PBUS_PCI_NV_1                                 0x00000804
#define NV_PBUS_PCI_NV_2                                 0x00000808
#   define NV_PBUS_PCI_NV_2_REVISION_ID                       0x000000FF
#   define NV_PBUS_PCI_NV_2_CLASS_CODE                        0xFFFFFF00


#define NV_PFIFO_INTR_0                                  0x00000100
#   define NV_PFIFO_INTR_0_CACHE_ERROR                          (1 << 0)
#   define NV_PFIFO_INTR_0_RUNOUT                               (1 << 4)
#   define NV_PFIFO_INTR_0_RUNOUT_OVERFLOW                      (1 << 8)
#   define NV_PFIFO_INTR_0_DMA_PUSHER                          (1 << 12)
#   define NV_PFIFO_INTR_0_DMA_PT                              (1 << 16)
#   define NV_PFIFO_INTR_0_SEMAPHORE                           (1 << 20)
#   define NV_PFIFO_INTR_0_ACQUIRE_TIMEOUT                     (1 << 24)
#define NV_PFIFO_INTR_EN_0                               0x00000140
#   define NV_PFIFO_INTR_EN_0_CACHE_ERROR                       (1 << 0)
#   define NV_PFIFO_INTR_EN_0_RUNOUT                            (1 << 4)
#   define NV_PFIFO_INTR_EN_0_RUNOUT_OVERFLOW                   (1 << 8)
#   define NV_PFIFO_INTR_EN_0_DMA_PUSHER                       (1 << 12)
#   define NV_PFIFO_INTR_EN_0_DMA_PT                           (1 << 16)
#   define NV_PFIFO_INTR_EN_0_SEMAPHORE                        (1 << 20)
#   define NV_PFIFO_INTR_EN_0_ACQUIRE_TIMEOUT                  (1 << 24)
#define NV_PFIFO_RAMHT                                   0x00000210
#   define NV_PFIFO_RAMHT_BASE_ADDRESS                        0x000001F0
#   define NV_PFIFO_RAMHT_SIZE                                0x00030000
#       define NV_PFIFO_RAMHT_SIZE_4K                             0x00000000
#       define NV_PFIFO_RAMHT_SIZE_8K                             0x00010000
#       define NV_PFIFO_RAMHT_SIZE_16K                            0x00020000
#       define NV_PFIFO_RAMHT_SIZE_32K                            0x00030000
#   define NV_PFIFO_RAMHT_SEARCH                              0x03000000
#       define NV_PFIFO_RAMHT_SEARCH_16                           0x00000000
#       define NV_PFIFO_RAMHT_SEARCH_32                           0x01000000
#       define NV_PFIFO_RAMHT_SEARCH_64                           0x02000000
#       define NV_PFIFO_RAMHT_SEARCH_128                          0x03000000
#define NV_PFIFO_RAMFC                                   0x00000214
#   define NV_PFIFO_RAMFC_BASE_ADDRESS1                       0x000001FC
#   define NV_PFIFO_RAMFC_SIZE                                0x00010000
#   define NV_PFIFO_RAMFC_BASE_ADDRESS2                       0x00FE0000
#define NV_PFIFO_RAMRO                                   0x00000218
#   define NV_PFIFO_RAMRO_BASE_ADDRESS                        0x000001FE
#   define NV_PFIFO_RAMRO_SIZE                                0x00010000
#define NV_PFIFO_RUNOUT_STATUS                           0x00000400
#   define NV_PFIFO_RUNOUT_STATUS_RANOUT                       (1 << 0)
#   define NV_PFIFO_RUNOUT_STATUS_LOW_MARK                     (1 << 4)
#   define NV_PFIFO_RUNOUT_STATUS_HIGH_MARK                    (1 << 8)
#define NV_PFIFO_MODE                                    0x00000504
#define NV_PFIFO_DMA                                     0x00000508
#define NV_PFIFO_CACHE1_PUSH0                            0x00001200
#   define NV_PFIFO_CACHE1_PUSH0_ACCESS                         (1 << 0)
#define NV_PFIFO_CACHE1_PUSH1                            0x00001204
#   define NV_PFIFO_CACHE1_PUSH1_CHID                         0x0000001F
#   define NV_PFIFO_CACHE1_PUSH1_MODE                         0x00000100
#define NV_PFIFO_CACHE1_STATUS                           0x00001214
#   define NV_PFIFO_CACHE1_STATUS_LOW_MARK                      (1 << 4)
#   define NV_PFIFO_CACHE1_STATUS_HIGH_MARK                     (1 << 8)
#define NV_PFIFO_CACHE1_DMA_PUSH                         0x00001220
#   define NV_PFIFO_CACHE1_DMA_PUSH_ACCESS                      (1 << 0)
#   define NV_PFIFO_CACHE1_DMA_PUSH_STATE                       (1 << 4)
#   define NV_PFIFO_CACHE1_DMA_PUSH_BUFFER                      (1 << 8)
#   define NV_PFIFO_CACHE1_DMA_PUSH_STATUS                     (1 << 12)
#   define NV_PFIFO_CACHE1_DMA_PUSH_ACQUIRE                    (1 << 16)
#define NV_PFIFO_CACHE1_DMA_FETCH                        0x00001224
#   define NV_PFIFO_CACHE1_DMA_FETCH_TRIG                     0x000000F8
#   define NV_PFIFO_CACHE1_DMA_FETCH_SIZE                     0x0000E000
#   define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS                 0x001F0000
#define NV_PFIFO_CACHE1_DMA_STATE                        0x00001228
#   define NV_PFIFO_CACHE1_DMA_STATE_METHOD_TYPE                (1 << 0)
#   define NV_PFIFO_CACHE1_DMA_STATE_METHOD                   0x00001FFC
#   define NV_PFIFO_CACHE1_DMA_STATE_SUBCHANNEL               0x0000E000
#   define NV_PFIFO_CACHE1_DMA_STATE_METHOD_COUNT             0x1FFC0000
#   define NV_PFIFO_CACHE1_DMA_STATE_ERROR                    0xE0000000
#       define NV_PFIFO_CACHE1_DMA_STATE_ERROR_NONE               0x00000000
#       define NV_PFIFO_CACHE1_DMA_STATE_ERROR_CALL               0x00000001
#       define NV_PFIFO_CACHE1_DMA_STATE_ERROR_NON_CACHE          0x00000002
#       define NV_PFIFO_CACHE1_DMA_STATE_ERROR_RETURN             0x00000003
#       define NV_PFIFO_CACHE1_DMA_STATE_ERROR_RESERVED_CMD       0x00000004
#       define NV_PFIFO_CACHE1_DMA_STATE_ERROR_PROTECTION         0x00000006
#define NV_PFIFO_CACHE1_DMA_INSTANCE                     0x0000122C
#   define NV_PFIFO_CACHE1_DMA_INSTANCE_ADDRESS               0x0000FFFF
#define NV_PFIFO_CACHE1_DMA_PUT                          0x00001240
#define NV_PFIFO_CACHE1_DMA_GET                          0x00001244
#define NV_PFIFO_CACHE1_DMA_SUBROUTINE                   0x0000124C
#   define NV_PFIFO_CACHE1_DMA_SUBROUTINE_RETURN_OFFSET       0x1FFFFFFC
#   define NV_PFIFO_CACHE1_DMA_SUBROUTINE_STATE                (1 << 0)
#define NV_PFIFO_CACHE1_DMA_DCOUNT                       0x000012A0
#   define NV_PFIFO_CACHE1_DMA_DCOUNT_VALUE                   0x00001FFC
#define NV_PFIFO_CACHE1_DMA_GET_JMP_SHADOW               0x000012A4
#   define NV_PFIFO_CACHE1_DMA_GET_JMP_SHADOW_OFFSET          0x1FFFFFFC
#define NV_PFIFO_CACHE1_DMA_RSVD_SHADOW                  0x000012A8
#define NV_PFIFO_CACHE1_DMA_DATA_SHADOW                  0x000012AC


#define NV_PGRAPH_CTX_CONTROL                            0x00000144
#   define NV_PGRAPH_CTX_CONTROL_MINIMUM_TIME                 0x00000003
#   define NV_PGRAPH_CTX_CONTROL_TIME                           (1 << 8)
#   define NV_PGRAPH_CTX_CONTROL_CHID                          (1 << 16)
#   define NV_PGRAPH_CTX_CONTROL_CHANGE                        (1 << 20)
#   define NV_PGRAPH_CTX_CONTROL_SWITCHING                     (1 << 24)
#   define NV_PGRAPH_CTX_CONTROL_DEVICE                        (1 << 28)
#define NV_PGRAPH_CTX_USER                               0x00000148
#   define NV_PGRAPH_CTX_USER_CHANNEL_3D                        (1 << 0)
#   define NV_PGRAPH_CTX_USER_CHANNEL_3D_VALID                  (1 << 4)
#   define NV_PGRAPH_CTX_USER_SUBCH                           0x0000E000
#   define NV_PGRAPH_CTX_USER_CHID                            0x1F000000
#   define NV_PGRAPH_CTX_USER_SINGLE_STEP                      (1 << 31)
#define NV_PGRAPH_CTX_SWITCH1                            0x0040014C
#   define NV_PGRAPH_CTX_SWITCH1_GRCLASS                      0x000000FF
#   define NV_PGRAPH_CTX_SWITCH1_CHROMA_KEY                    (1 << 12)
#   define NV_PGRAPH_CTX_SWITCH1_SWIZZLE                       (1 << 14)
#   define NV_PGRAPH_CTX_SWITCH1_PATCH_CONFIG                 0x00038000
#   define NV_PGRAPH_CTX_SWITCH1_SYNCHRONIZE                   (1 << 18)
#   define NV_PGRAPH_CTX_SWITCH1_ENDIAN_MODE                   (1 << 19)
#   define NV_PGRAPH_CTX_SWITCH1_CLASS_TYPE                    (1 << 22)
#   define NV_PGRAPH_CTX_SWITCH1_SINGLE_STEP                   (1 << 23)
#   define NV_PGRAPH_CTX_SWITCH1_PATCH_STATUS                  (1 << 24)
#   define NV_PGRAPH_CTX_SWITCH1_CONTEXT_SURFACE0              (1 << 25)
#   define NV_PGRAPH_CTX_SWITCH1_CONTEXT_SURFACE1              (1 << 26)
#   define NV_PGRAPH_CTX_SWITCH1_CONTEXT_PATTERN               (1 << 27)
#   define NV_PGRAPH_CTX_SWITCH1_CONTEXT_ROP                   (1 << 28)
#   define NV_PGRAPH_CTX_SWITCH1_CONTEXT_BETA1                 (1 << 29)
#   define NV_PGRAPH_CTX_SWITCH1_CONTEXT_BETA4                 (1 << 30)
#   define NV_PGRAPH_CTX_SWITCH1_VOLATILE_RESET                (1 << 31)
#define NV_PGRAPH_CHANNEL_CTX_TABLE                      0x00000780
#   define NV_PGRAPH_CHANNEL_CTX_TABLE_INST                   0x0000FFFF
#define NV_PGRAPH_CHANNEL_CTX_POINTER                    0x00000784
#   define NV_PGRAPH_CHANNEL_CTX_POINTER_INST                 0x0000FFFF
#define NV_PGRAPH_CHANNEL_CTX_TRIGGER                    0x00000788
#   define NV_PGRAPH_CHANNEL_CTX_TRIGGER_READ_IN                (1 << 0)
#   define NV_PGRAPH_CHANNEL_CTX_TRIGGER_WRITE_OUT              (1 << 1)


#define NV_PCRTC_INTR_0                                  0x00000100
#   define NV_PCRTC_INTR_0_VBLANK                               (1 << 0)
#define NV_PCRTC_INTR_EN_0                               0x00000140
#   define NV_PCRTC_INTR_EN_0_VBLANK                            (1 << 0)
#define NV_PCRTC_START                                   0x00000800
#define NV_PCRTC_CONFIG                                  0x00000804


#define NV_PTIMER_INTR_0                                 0x00000100
#   define NV_PTIMER_INTR_0_ALARM                               (1 << 0)
#define NV_PTIMER_INTR_EN_0                              0x00000140
#   define NV_PTIMER_INTR_EN_0_ALARM                            (1 << 0)
#define NV_PTIMER_NUMERATOR                              0x00000200
#define NV_PTIMER_DENOMINATOR                            0x00000210
#define NV_PTIMER_TIME_0                                 0x00000400
#define NV_PTIMER_TIME_1                                 0x00000410
#define NV_PTIMER_ALARM_0                                0x00000420


#define NV_PFB_CFG0                                      0x00000200
#   define NV_PFB_CFG0_PART                                   0x00000003
#define NV_PFB_CSTATUS                                   0x0000020C


#define NV_PRAMDAC_NVPLL_COEFF                           0x00000500
#   define NV_PRAMDAC_NVPLL_COEFF_MDIV                        0x000000FF
#   define NV_PRAMDAC_NVPLL_COEFF_NDIV                        0x0000FF00
#   define NV_PRAMDAC_NVPLL_COEFF_PDIV                        0x00070000
#define NV_PRAMDAC_MPLL_COEFF                            0x00000504
#   define NV_PRAMDAC_MPLL_COEFF_MDIV                         0x000000FF
#   define NV_PRAMDAC_MPLL_COEFF_NDIV                         0x0000FF00
#   define NV_PRAMDAC_MPLL_COEFF_PDIV                         0x00070000
#define NV_PRAMDAC_VPLL_COEFF                            0x00000508
#   define NV_PRAMDAC_VPLL_COEFF_MDIV                         0x000000FF
#   define NV_PRAMDAC_VPLL_COEFF_NDIV                         0x0000FF00
#   define NV_PRAMDAC_VPLL_COEFF_PDIV                         0x00070000
#define NV_PRAMDAC_PLL_TEST_COUNTER                      0x00000514
#   define NV_PRAMDAC_PLL_TEST_COUNTER_NOOFIPCLKS             0x000003FF
#   define NV_PRAMDAC_PLL_TEST_COUNTER_VALUE                  0x0000FFFF
#   define NV_PRAMDAC_PLL_TEST_COUNTER_ENABLE                  (1 << 16)
#   define NV_PRAMDAC_PLL_TEST_COUNTER_RESET                   (1 << 20)
#   define NV_PRAMDAC_PLL_TEST_COUNTER_SOURCE                 0x03000000
#   define NV_PRAMDAC_PLL_TEST_COUNTER_VPLL2_LOCK              (1 << 27)
#   define NV_PRAMDAC_PLL_TEST_COUNTER_PDIV_RST                (1 << 28)
#   define NV_PRAMDAC_PLL_TEST_COUNTER_NVPLL_LOCK              (1 << 29)
#   define NV_PRAMDAC_PLL_TEST_COUNTER_MPLL_LOCK               (1 << 30)
#   define NV_PRAMDAC_PLL_TEST_COUNTER_VPLL_LOCK               (1 << 31)


#define NV_USER_DMA_PUT                                  0x40
#define NV_USER_DMA_GET                                  0x44
#define NV_USER_REF                                      0x48



/* DMA objects */
#define NV_DMA_FROM_MEMORY_CLASS                         0x02
#define NV_DMA_TO_MEMORY_CLASS                           0x03
#define NV_DMA_IN_MEMORY_CLASS                           0x3d

#define NV_DMA_CLASS                                          0x00000FFF
#define NV_DMA_PAGE_TABLE                                      (1 << 12)
#define NV_DMA_PAGE_ENTRY                                      (1 << 13)
#define NV_DMA_FLAGS_ACCESS                                    (1 << 14)
#define NV_DMA_FLAGS_MAPPING_COHERENCY                         (1 << 15)
#define NV_DMA_TARGET                                         0x00030000
#   define NV_DMA_TARGET_NVM                                      0x00000000
#   define NV_DMA_TARGET_NVM_TILED                                0x00010000
#   define NV_DMA_TARGET_PCI                                      0x00020000
#   define NV_DMA_TARGET_AGP                                      0x00030000


#define NV_RAMHT_HANDLE                                      0xFFFFFFFF
#define NV_RAMHT_INSTANCE                                    0x0000FFFF
#define NV_RAMHT_ENGINE                                      0x00030000
#   define NV_RAMHT_ENGINE_SW                                     0x00000000
#   define NV_RAMHT_ENGINE_GRAPHICS                               0x00010000
#   define NV_RAMHT_ENGINE_DVD                                    0x00020000
#define NV_RAMHT_CHID                                        0x1F000000
#define NV_RAMHT_STATUS                                      0x80000000



/* graphic classes and methods */
#define NV_SET_OBJECT                                        0x00000000

#define NV_KELVIN_PRIMITIVE                                0x00000097
#   define NV097_NO_OPERATION                                     0x00970100
#   define NV097_WAIT_FOR_IDLE                                    0x00970110
#   define NV097_SET_CONTEXT_DMA_NOTIFIES                         0x00970180
#   define NV097_SET_CONTEXT_DMA_A                                0x00970184
#   define NV097_SET_CONTEXT_DMA_B                                0x00970188
#   define NV097_SET_CONTEXT_DMA_STATE                            0x00970190
#   define NV097_SET_CONTEXT_DMA_VERTEX_A                         0x0097019c
#   define NV097_SET_CONTEXT_DMA_VERTEX_B                         0x009701a0
#   define NV097_SET_CONTEXT_DMA_SEMAPHORE                        0x009701a4
#   define NV097_SET_SEMAPHORE_OFFSET                             0x00971d6c
#   define NV097_BACK_END_WRITE_SEMAPHORE_RELEASE                 0x00971d70



#define NV_MEMORY_TO_MEMORY_FORMAT                           0x00000039
#   define NV_MEMORY_TO_MEMORY_FORMAT_DMA_NOTIFY                  0x00390180
#   define NV_MEMORY_TO_MEMORY_FORMAT_DMA_SOURCE                  0x00390184




#define NV2A_CRYSTAL_FREQ 13500000
#define NV2A_NUM_CHANNELS 32
#define NV2A_NUM_SUBCHANNELS 8



enum FifoMode {
    FIFO_PIO = 0,
    FIFO_DMA = 1,
};

enum RAMHTEngine {
    ENGINE_SOFTWARE = 0,
    ENGINE_GRAPHICS = 1,
    ENGINE_DVD = 2,
};



typedef struct RAMHTEntry {
    uint32_t handle;
    hwaddr instance;
    enum RAMHTEngine engine;
    unsigned int channel_id : 5;
    bool valid;
} RAMHTEntry;


typedef struct DMAObject {
    unsigned int dma_class;
    hwaddr start;
    hwaddr limit;
} DMAObject;


typedef struct GraphicsObject {
    uint8_t graphic_class;
    union {
        struct {
            hwaddr dma_notifies;
        } memory_to_memory_format;

        struct {
            hwaddr dma_notifies;
            hwaddr dma_a;
            hwaddr dma_b;
            hwaddr dma_state;
            hwaddr dma_vertex_a;
            hwaddr dma_vertex_b;
            hwaddr dma_semaphore;
            unsigned int semaphore_offset;
        } kelvin_primitive;
    } data;
} GraphicsObject;

typedef struct GraphicsSubchannelData {
    hwaddr object_instance;
    GraphicsObject object;
    uint32_t object_cache[5];
} GraphicsSubchannelData;

typedef struct GraphicsContext {
    bool channel_3d;
    unsigned int channel_id;
    unsigned int subchannel;

    GraphicsSubchannelData subchannel_data[NV2A_NUM_SUBCHANNELS];
} GraphicsContext;




typedef struct Cache1State {
    unsigned int channel_id;
    enum FifoMode mode;

    bool push_enabled;
    bool dma_push_enabled;

    /* Pusher state */
    hwaddr dma_instance;
    bool method_nonincreasing;
    unsigned int method : 14;
    unsigned int subchannel : 3;
    unsigned int method_count : 24;
    uint32_t dcount;
    bool subroutine_active;
    hwaddr subroutine_return;
    hwaddr get_jmp_shadow;
    uint32_t rsvd_shadow;
    uint32_t data_shadow;
    uint32_t error;

    /* Puller state */
    uint8_t bound_engines[NV2A_NUM_SUBCHANNELS];
    unsigned int last_engine : 5;
} Cache1State;

typedef struct ChannelControl {
    hwaddr dma_put;
    hwaddr dma_get;
    uint32_t ref;
} ChannelControl;



typedef struct NV2AState {
    PCIDevice dev;
    qemu_irq irq;

    VGACommonState vga;

    MemoryRegion vram;
    MemoryRegion ramin;
    uint8_t *ramin_ptr;

    MemoryRegion mmio;

    MemoryRegion block_mmio[NV_NUM_BLOCKS];

    struct {
        uint32_t pending_interrupts;
        uint32_t enabled_interrupts;
    } pmc;

    struct {
        uint32_t pending_interrupts;
        uint32_t enabled_interrupts;

        hwaddr ramht_address;
        unsigned int ramht_size;
        uint32_t ramht_search;

        hwaddr ramfc_address1;
        hwaddr ramfc_address2;
        unsigned int ramfc_size;

        /* Weather the fifo chanels are PIO or DMA */
        uint32_t channel_modes;

        uint32_t channels_pending_push;

        Cache1State cache1;
    } pfifo;

    struct {
        uint32_t pending_interrupts;
        uint32_t enabled_interrupts;

        uint32_t numerator;
        uint32_t denominator;

        uint32_t alarm_time;
    } ptimer;

    struct {
        hwaddr context_table;
        hwaddr context_pointer;

        unsigned int channel_id;
        GraphicsContext context[NV2A_NUM_CHANNELS];
    } pgraph;

    struct {
        uint32_t pending_interrupts;
        uint32_t enabled_interrupts;

        hwaddr start;
    } pcrtc;

    struct {
        uint32_t core_clock_coeff;
        uint64_t core_clock_freq;
        uint32_t memory_clock_coeff;
        uint32_t video_clock_coeff;
    } pramdac;

    struct {
        ChannelControl channel_control[NV2A_NUM_CHANNELS];
    } user;

} NV2AState;


#define NV2A_DEVICE(obj) \
    OBJECT_CHECK(NV2AState, (obj), "nv2a")



static void nv2a_update_irq(NV2AState *d)
{
    /* PFIFO */
    if (d->pfifo.pending_interrupts & d->pfifo.enabled_interrupts) {
        d->pmc.pending_interrupts |= NV_PMC_INTR_0_PFIFO;
    } else {
        d->pmc.pending_interrupts &= ~NV_PMC_INTR_0_PFIFO;
    }

    /* PCRTC */
    if (d->pcrtc.pending_interrupts & d->pcrtc.enabled_interrupts) {
        d->pmc.pending_interrupts |= NV_PMC_INTR_0_PCRTC;
    } else {
        d->pmc.pending_interrupts &= ~NV_PMC_INTR_0_PCRTC;
    }

    if (d->pmc.pending_interrupts && d->pmc.enabled_interrupts) {
        qemu_irq_raise(d->irq);
    } else {
        qemu_irq_lower(d->irq);
    }
}

static uint32_t nv2a_ramht_hash(NV2AState *d,
                                uint32_t handle)
{
    uint32_t hash = 0;
    /* XXX: Think this is different to what nouveau calculates... */
    uint32_t bits = ffs(d->pfifo.ramht_size)-2;

    while (handle) {
        hash ^= (handle & ((1 << bits) - 1));
        handle >>= bits;
    }
    hash ^= d->pfifo.cache1.channel_id << (bits - 4);

    return hash;
}


static RAMHTEntry nv2a_lookup_ramht(NV2AState *d, uint32_t handle)
{
    uint32_t hash;
    uint8_t *entry_ptr;
    uint32_t entry_handle;
    uint32_t entry_context;


    hash = nv2a_ramht_hash(d, handle);
    assert(hash * 8 < d->pfifo.ramht_size);

    entry_ptr = d->ramin_ptr + d->pfifo.ramht_address + hash * 8;

    entry_handle = le32_to_cpupu((uint32_t*)entry_ptr);
    entry_context = le32_to_cpupu((uint32_t*)(entry_ptr + 4));

    return (RAMHTEntry){
        .handle = entry_handle,
        .instance = (entry_context & NV_RAMHT_INSTANCE) << 4,
        .engine = (entry_context & NV_RAMHT_ENGINE) >> 16,
        .channel_id = (entry_context & NV_RAMHT_CHID) >> 24,
        .valid = entry_context & NV_RAMHT_STATUS,
    };
}


static DMAObject nv2a_load_dma_object(NV2AState *d,
                                      hwaddr address)
{
    uint8_t *dma_ptr;
    uint32_t flags;

    dma_ptr = d->ramin_ptr + address;
    flags = le32_to_cpupu((uint32_t*)dma_ptr);

    return (DMAObject){
        .dma_class = flags & NV_DMA_CLASS,

        /* XXX: Why is this layout different to nouveau? */
        .limit = le32_to_cpupu((uint32_t*)(dma_ptr + 4)),
        .start = le32_to_cpupu((uint32_t*)(dma_ptr + 8)) & (~3),
    };
}

static GraphicsObject nv2a_load_graphics_object(NV2AState *d,
                                                hwaddr address)
{
    uint8_t *obj_ptr;
    uint32_t switch1, switch2, switch3;

    obj_ptr = d->ramin_ptr + address;

    switch1 = le32_to_cpupu((uint32_t*)obj_ptr);
    switch2 = le32_to_cpupu((uint32_t*)(obj_ptr+4));
    switch3 = le32_to_cpupu((uint32_t*)(obj_ptr+8));

    return (GraphicsObject){
        .graphic_class = switch1 & NV_PGRAPH_CTX_SWITCH1_GRCLASS,
    };
}


static void nv2a_pgraph_method(NV2AState *d,
                               unsigned int subchannel,
                               unsigned int method,
                               uint32_t parameter)
{
    GraphicsContext *context = &d->pgraph.context[d->pgraph.channel_id];
    GraphicsSubchannelData *subchannel_data =
        &context->subchannel_data[subchannel];
    GraphicsObject *object = &subchannel_data->object;

    NV2A_DPRINTF("nv2a pgraph method: 0x%x, 0x%x, 0x%x\n",
                 subchannel, method, parameter);

    if (method == NV_SET_OBJECT) {
        subchannel_data->object_instance = parameter;
        *object = nv2a_load_graphics_object(d, parameter);

        return;
    }

    DMAObject dma_semaphore;

    switch ((object->graphic_class << 16) | method) {
        case NV_MEMORY_TO_MEMORY_FORMAT_DMA_NOTIFY:
            object->data.memory_to_memory_format.dma_notifies = parameter;
            break;


        case NV097_NO_OPERATION:
            break;
        case NV097_WAIT_FOR_IDLE:
            break;
        case NV097_SET_CONTEXT_DMA_NOTIFIES:
            object->data.kelvin_primitive.dma_notifies = parameter;
            break;
        case NV097_SET_CONTEXT_DMA_A:
            object->data.kelvin_primitive.dma_a = parameter;
            break;
        case NV097_SET_CONTEXT_DMA_B:
            object->data.kelvin_primitive.dma_b = parameter;
            break;
        case NV097_SET_CONTEXT_DMA_STATE:
            object->data.kelvin_primitive.dma_state = parameter;
            break;
        case NV097_SET_CONTEXT_DMA_VERTEX_A:
            object->data.kelvin_primitive.dma_vertex_a = parameter;
            break;
        case NV097_SET_CONTEXT_DMA_VERTEX_B:
            object->data.kelvin_primitive.dma_vertex_b = parameter;
            break;
        case NV097_SET_CONTEXT_DMA_SEMAPHORE:
            object->data.kelvin_primitive.dma_semaphore = parameter;
            break;
        case NV097_SET_SEMAPHORE_OFFSET:
            object->data.kelvin_primitive.semaphore_offset = parameter;
            break;
        case NV097_BACK_END_WRITE_SEMAPHORE_RELEASE:
            dma_semaphore = nv2a_load_dma_object(d,
                                object->data.kelvin_primitive.dma_semaphore);

            assert(object->data.kelvin_primitive.semaphore_offset
                    < dma_semaphore.limit);

            stl_le_phys(dma_semaphore.start
                         + object->data.kelvin_primitive.semaphore_offset,
                        parameter);
            break;
        default:
            NV2A_DPRINTF("    unhandled  (0x%02x 0x%08x  -  0x%x)\n",
                         object->graphic_class, method, parameter);
            break;
    }
}


static void nv2a_cache_push(NV2AState *d,
                            unsigned int subchannel,
                            unsigned int method,
                            uint32_t parameter,
                            bool nonincreasing)
{
    Cache1State *state = &d->pfifo.cache1;


    //NV2A_DPRINTF("nv2a cache push: 0x%x, 0x%x, 0x%x, %d\n",
    //             subchannel, method, parameter, nonincreasing);

    /* Don't bother emulating the pfifo cache roundtrip for now,
     * emulate the puller on the method immediately... */

    if (method == 0) {
        RAMHTEntry entry = nv2a_lookup_ramht(d, parameter);
        assert(entry.valid);

        assert(entry.channel_id == state->channel_id);

        switch (entry.engine) {
        case ENGINE_SOFTWARE:
            /* TODO */
            assert(false);
            break;
        case ENGINE_GRAPHICS:
            nv2a_pgraph_method(d, subchannel, 0, entry.instance);
            break;
        default:
            assert(false);
            break;
        }

        /* the engine is bound to the subchannel */
        state->bound_engines[subchannel] = entry.engine;
        state->last_engine = state->bound_engines[subchannel];
    } else if (method >= 0x100) {
        /* method passed to engine */

        /* methods that take objects.
         * XXX: This range is probably not correct for the nv2a */
        if (method >= 0x180 && method < 0x200) {
            RAMHTEntry entry = nv2a_lookup_ramht(d, parameter);
            assert(entry.valid);
            assert(entry.channel_id == state->channel_id);
            parameter = entry.instance;
        }


        switch (state->bound_engines[subchannel]) {
        case ENGINE_SOFTWARE:
            assert(false);
            break;
        case ENGINE_GRAPHICS:
            nv2a_pgraph_method(d, subchannel, method, parameter);
            break;
        default:
            assert(false);
            break;
        }

        state->last_engine = state->bound_engines[subchannel];
    } else {
        assert(false);
    }

}

static void nv2a_run_pusher(NV2AState *d) {
    uint8_t channel_id;
    ChannelControl *control;
    Cache1State *state;
    DMAObject dma;
    uint32_t word;

    /* TODO: How is cache1 selected? */
    state = &d->pfifo.cache1;
    channel_id = state->channel_id;
    control = &d->user.channel_control[channel_id];

    /* only handling DMA for now... */

    /* Channel running DMA */
    assert(d->pfifo.channel_modes & (1 << channel_id));

    assert(state->mode == FIFO_DMA);
    assert(state->push_enabled);
    assert(state->dma_push_enabled);

    /* No pending errors... */
    assert(state->error == NV_PFIFO_CACHE1_DMA_STATE_ERROR_NONE);

    dma = nv2a_load_dma_object(d, state->dma_instance);
    assert(dma.dma_class == NV_DMA_FROM_MEMORY_CLASS);

    NV2A_DPRINTF("nv2a DMA pusher: 0x%llx - 0x%llx, 0x%llx - 0x%llx\n",
                 dma.start, dma.limit, control->dma_get, control->dma_put);

    /* based on the convenient pseudocode in envytools */
    while (control->dma_get != control->dma_put) {
        if (control->dma_get >= dma.limit) {

            state->error = NV_PFIFO_CACHE1_DMA_STATE_ERROR_PROTECTION;
            break;
        }

        word = ldl_le_phys(dma.start+control->dma_get);
        control->dma_get += 4;

        if (state->method_count) {
            /* data word of methods command */
            state->data_shadow = word;

            nv2a_cache_push(d, state->subchannel, state->method, word,
                            state->method_nonincreasing);

            if (!state->method_nonincreasing) {
                state->method += 4;
            }
            state->method_count--;
            state->dcount++;
        } else {
            /* no command active - this is the first word of a new one */
            state->rsvd_shadow = word;
            /* match all forms */
            if ((word & 0xe0000003) == 0x20000000) {
                /* old jump */
                state->get_jmp_shadow = control->dma_get;
                control->dma_get = word & 0x1fffffff;
                NV2A_DPRINTF("nv2a pb OLD_JMP 0x%llx\n", control->dma_get);
            } else if ((word & 3) == 1) {
                /* jump */
                state->get_jmp_shadow = control->dma_get;
                control->dma_get = word & 0xfffffffc;
                NV2A_DPRINTF("nv2a pb JMP 0x%llx\n", control->dma_get);
            } else if ((word & 3) == 2) {
                /* call */
                if (state->subroutine_active) {
                    state->error = NV_PFIFO_CACHE1_DMA_STATE_ERROR_CALL;
                    break;
                }
                state->subroutine_return = control->dma_get;
                state->subroutine_active = true;
                control->dma_get = word & 0xfffffffc;
                NV2A_DPRINTF("nv2a pb CALL 0x%llx\n", control->dma_get);
            } else if (word == 0x00020000) {
                /* return */
                if (!state->subroutine_active) {
                    state->error = NV_PFIFO_CACHE1_DMA_STATE_ERROR_RETURN;
                    break;
                }
                control->dma_get = state->subroutine_return;
                state->subroutine_active = false;
                NV2A_DPRINTF("nv2a pb RET 0x%llx\n", control->dma_get);
            } else if ((word & 0xe0030003) == 0) {
                /* increasing methods */
                state->method = word & 0x1fff;
                state->subchannel = (word >> 13) & 7;
                state->method_count = (word >> 18) & 0x7ff;
                state->method_nonincreasing = false;
                state->dcount = 0;
            } else if ((word & 0xe0030003) == 0x40000000) {
                /* non-increasing methods */
                state->method = word & 0x1fff;
                state->subchannel = (word >> 13) & 7;
                state->method_count = (word >> 18) & 0x7ff;
                state->method_nonincreasing = true;
                state->dcount = 0;
            } else {
                state->error = NV_PFIFO_CACHE1_DMA_STATE_ERROR_RESERVED_CMD;
                break;
            }
        }
    }

    if (state->error) {
        NV2A_DPRINTF("nv2a pb error: %d\n", state->error);
        d->pfifo.pending_interrupts |= NV_PFIFO_INTR_0_DMA_PUSHER;
        nv2a_update_irq(d);
    }
}






/* PMC - card master control */
static uint64_t nv2a_pmc_read(void *opaque,
                              hwaddr addr, unsigned int size)
{
    NV2AState *d = opaque;

    uint64_t r = 0;
    switch (addr) {
    case NV_PMC_BOOT_0:
        /* chipset and stepping:
         * NV2A, A02, Rev 0 */

        r = 0x02A000A2;
        break;
    case NV_PMC_INTR_0:
        /* Shows which functional units have pending IRQ */
        r = d->pmc.pending_interrupts;
        break;
    case NV_PMC_INTR_EN_0:
        /* Selects which functional units can cause IRQs */
        r = d->pmc.enabled_interrupts;
        break;
    default:
        break;
    }

    NV2A_DPRINTF("nv2a PMC: read [0x%llx] -> 0x%llx\n", addr, r);
    return r;
}
static void nv2a_pmc_write(void *opaque, hwaddr addr,
                           uint64_t val, unsigned int size)
{
    NV2AState *d = opaque;

    NV2A_DPRINTF("nv2a PMC: [0x%llx] = 0x%02llx\n", addr, val);

    switch (addr) {
    case NV_PMC_INTR_0:
        /* the bits of the interrupts to clear are wrtten */
        d->pmc.pending_interrupts &= ~val;
        nv2a_update_irq(d);
        break;
    case NV_PMC_INTR_EN_0:
        d->pmc.enabled_interrupts = val;
        nv2a_update_irq(d);
        break;
    default:
        break;
    }
}


/* PBUS - bus control */
static uint64_t nv2a_pbus_read(void *opaque,
                               hwaddr addr, unsigned int size)
{
    NV2AState *d = opaque;

    uint64_t r = 0;
    switch (addr) {
    case NV_PBUS_PCI_NV_0:
        r = pci_get_long(d->dev.config + PCI_VENDOR_ID);
        break;
    case NV_PBUS_PCI_NV_1:
        r = pci_get_long(d->dev.config + PCI_COMMAND);
        break;
    case NV_PBUS_PCI_NV_2:
        r = pci_get_long(d->dev.config + PCI_CLASS_REVISION);
        break;
    default:
        break;
    }

    NV2A_DPRINTF("nv2a PBUS: read [0x%llx] -> 0x%llx\n", addr, r);
    return r;
}
static void nv2a_pbus_write(void *opaque, hwaddr addr,
                            uint64_t val, unsigned int size)
{
    NV2AState *d = opaque;

    NV2A_DPRINTF("nv2a PBUS: [0x%llx] = 0x%02llx\n", addr, val);

    switch (addr) {
    case NV_PBUS_PCI_NV_1:
        pci_set_long(d->dev.config + PCI_COMMAND, val);
    default:
        break;
    }
}


/* PFIFO - MMIO and DMA FIFO submission to PGRAPH and VPE */
static uint64_t nv2a_pfifo_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    NV2AState *d = opaque;

    uint64_t r = 0;
    switch (addr) {
    case NV_PFIFO_INTR_0:
        r = d->pfifo.pending_interrupts;
        break;
    case NV_PFIFO_INTR_EN_0:
        r = d->pfifo.enabled_interrupts;
        break;
    case NV_PFIFO_RAMHT:
        r = ( (d->pfifo.ramht_address >> 12) << 4)
             | (d->pfifo.ramht_search << 24);

        switch (d->pfifo.ramht_size) {
            case 4096:
                r |= NV_PFIFO_RAMHT_SIZE_4K;
                break;
            case 8192:
                r |= NV_PFIFO_RAMHT_SIZE_8K;
                break;
            case 16384:
                r |= NV_PFIFO_RAMHT_SIZE_16K;
                break;
            case 32768:
                r |= NV_PFIFO_RAMHT_SIZE_32K;
                break;
            default:
                break;
        }

        break;
    case NV_PFIFO_RAMFC:
        r = ( (d->pfifo.ramfc_address1 >> 10) << 2)
             | (d->pfifo.ramfc_size << 16)
             | ((d->pfifo.ramfc_address2 >> 10) << 17);
        break;
    case NV_PFIFO_RUNOUT_STATUS:
        r = NV_PFIFO_RUNOUT_STATUS_LOW_MARK; /* low mark empty */
        break;
    case NV_PFIFO_MODE:
        r = d->pfifo.channel_modes;
        break;
    case NV_PFIFO_DMA:
        r = d->pfifo.channels_pending_push;
        break;

    case NV_PFIFO_CACHE1_PUSH0:
        r = d->pfifo.cache1.push_enabled;
        break;
    case NV_PFIFO_CACHE1_PUSH1:
        r = (d->pfifo.cache1.channel_id & NV_PFIFO_CACHE1_PUSH1_CHID)
            | (d->pfifo.cache1.mode << 8);
        break;
    case NV_PFIFO_CACHE1_STATUS:
        r = (1 << 4); /* low mark empty */
        break;
    case NV_PFIFO_CACHE1_DMA_PUSH:
        r = d->pfifo.cache1.dma_push_enabled
            | (1 << 8) /* buffer empty */
            | (1 << 12); /* status suspended */
        break;
    case NV_PFIFO_CACHE1_DMA_STATE:
        r = d->pfifo.cache1.method_nonincreasing
            | (d->pfifo.cache1.method << 2)
            | (d->pfifo.cache1.subchannel << 13)
            | (d->pfifo.cache1.method_count << 18)
            | (d->pfifo.cache1.error << 29);
        break;
    case NV_PFIFO_CACHE1_DMA_INSTANCE:
        r = (d->pfifo.cache1.dma_instance >> 4)
             & NV_PFIFO_CACHE1_DMA_INSTANCE_ADDRESS;
        break;
    case NV_PFIFO_CACHE1_DMA_PUT:
        r = d->user.channel_control[d->pfifo.cache1.channel_id].dma_put;
        break;
    case NV_PFIFO_CACHE1_DMA_GET:
        r = d->user.channel_control[d->pfifo.cache1.channel_id].dma_get;
        break;
    case NV_PFIFO_CACHE1_DMA_SUBROUTINE:
        r = d->pfifo.cache1.subroutine_return
            | d->pfifo.cache1.subroutine_active;
        break;
    case NV_PFIFO_CACHE1_DMA_DCOUNT:
        r = d->pfifo.cache1.dcount;
        break;
    case NV_PFIFO_CACHE1_DMA_GET_JMP_SHADOW:
        r = d->pfifo.cache1.get_jmp_shadow;
        break;
    case NV_PFIFO_CACHE1_DMA_RSVD_SHADOW:
        r = d->pfifo.cache1.rsvd_shadow;
        break;
    case NV_PFIFO_CACHE1_DMA_DATA_SHADOW:
        r = d->pfifo.cache1.data_shadow;
        break;
    default:
        break;
    }

    NV2A_DPRINTF("nv2a PFIFO: read [0x%llx] -> 0x%llx\n", addr, r);
    return r;
}
static void nv2a_pfifo_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    NV2AState *d = opaque;

    NV2A_DPRINTF("nv2a PFIFO: [0x%llx] = 0x%02llx\n", addr, val);

    switch (addr) {
    case NV_PFIFO_INTR_0:
        d->pfifo.pending_interrupts &= ~val;
        nv2a_update_irq(d);
        break;
    case NV_PFIFO_INTR_EN_0:
        d->pfifo.enabled_interrupts = val;
        nv2a_update_irq(d);
        break;
    case NV_PFIFO_RAMHT:
        d->pfifo.ramht_address =
            ((val & NV_PFIFO_RAMHT_BASE_ADDRESS) >> 4) << 12;
        switch (val & NV_PFIFO_RAMHT_SIZE) {
            case NV_PFIFO_RAMHT_SIZE_4K:
                d->pfifo.ramht_size = 4096;
                break;
            case NV_PFIFO_RAMHT_SIZE_8K:
                d->pfifo.ramht_size = 8192;
                break;
            case NV_PFIFO_RAMHT_SIZE_16K:
                d->pfifo.ramht_size = 16384;
                break;
            case NV_PFIFO_RAMHT_SIZE_32K:
                d->pfifo.ramht_size = 32768;
                break;
            default:
                d->pfifo.ramht_size = 0;
                break;
        }
        d->pfifo.ramht_search = (val & NV_PFIFO_RAMHT_SEARCH) >> 24;
        break;
    case NV_PFIFO_RAMFC:
        d->pfifo.ramfc_address1 =
            ((val & NV_PFIFO_RAMFC_BASE_ADDRESS1) >> 2) << 10;
        d->pfifo.ramfc_size = (val & NV_PFIFO_RAMFC_SIZE) >> 16;
        d->pfifo.ramfc_address2 =
            ((val & NV_PFIFO_RAMFC_BASE_ADDRESS2) >> 17) << 10;
        break;
    case NV_PFIFO_MODE:
        d->pfifo.channel_modes = val;
        break;
    case NV_PFIFO_DMA:
        d->pfifo.channels_pending_push = val;
        break;

    case NV_PFIFO_CACHE1_PUSH0:
        d->pfifo.cache1.push_enabled = val & NV_PFIFO_CACHE1_PUSH0_ACCESS;
        break;
    case NV_PFIFO_CACHE1_PUSH1:
        d->pfifo.cache1.channel_id = val & NV_PFIFO_CACHE1_PUSH1_CHID;
        d->pfifo.cache1.mode = (val & NV_PFIFO_CACHE1_PUSH1_MODE) >> 8;
        assert(d->pfifo.cache1.channel_id < NV2A_NUM_CHANNELS);
        break;
    case NV_PFIFO_CACHE1_DMA_PUSH:
        d->pfifo.cache1.dma_push_enabled =
            val & NV_PFIFO_CACHE1_DMA_PUSH_ACCESS;
        break;
    case NV_PFIFO_CACHE1_DMA_STATE:
        d->pfifo.cache1.method_nonincreasing =
            (val & NV_PFIFO_CACHE1_DMA_STATE_METHOD_TYPE);
        d->pfifo.cache1.method = (val & NV_PFIFO_CACHE1_DMA_STATE_METHOD);
        d->pfifo.cache1.subchannel =
            (val & NV_PFIFO_CACHE1_DMA_STATE_SUBCHANNEL) >> 13;
        d->pfifo.cache1.method_count =
            (val & NV_PFIFO_CACHE1_DMA_STATE_METHOD_COUNT) >> 18;
        d->pfifo.cache1.error =
            (val & NV_PFIFO_CACHE1_DMA_STATE_ERROR) >> 29;
        break;
    case NV_PFIFO_CACHE1_DMA_INSTANCE:
        d->pfifo.cache1.dma_instance =
            (val & NV_PFIFO_CACHE1_DMA_INSTANCE_ADDRESS) << 4;
        break;
    case NV_PFIFO_CACHE1_DMA_PUT:
        d->user.channel_control[d->pfifo.cache1.channel_id].dma_put = val;
        break;
    case NV_PFIFO_CACHE1_DMA_GET:
        d->user.channel_control[d->pfifo.cache1.channel_id].dma_get = val;
        break;
    case NV_PFIFO_CACHE1_DMA_SUBROUTINE:
        d->pfifo.cache1.subroutine_return =
            (val & NV_PFIFO_CACHE1_DMA_SUBROUTINE_RETURN_OFFSET);
        d->pfifo.cache1.subroutine_active =
            (val & NV_PFIFO_CACHE1_DMA_SUBROUTINE_STATE);
        break;
    case NV_PFIFO_CACHE1_DMA_DCOUNT:
        d->pfifo.cache1.dcount =
            (val & NV_PFIFO_CACHE1_DMA_DCOUNT_VALUE);
        break;
    case NV_PFIFO_CACHE1_DMA_GET_JMP_SHADOW:
        d->pfifo.cache1.get_jmp_shadow =
            (val & NV_PFIFO_CACHE1_DMA_GET_JMP_SHADOW_OFFSET);
        break;
    case NV_PFIFO_CACHE1_DMA_RSVD_SHADOW:
        d->pfifo.cache1.rsvd_shadow = val;
        break;
    case NV_PFIFO_CACHE1_DMA_DATA_SHADOW:
        d->pfifo.cache1.data_shadow = val;
        break;
    default:
        break;
    }
}


static uint64_t nv2a_prma_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    NV2A_DPRINTF("nv2a PRMA: read [0x%llx]\n", addr);
    return 0;
}
static void nv2a_prma_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    NV2A_DPRINTF("nv2a PRMA: [0x%llx] = 0x%02llx\n", addr, val);
}


static uint64_t nv2a_pvideo_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    NV2A_DPRINTF("nv2a PVIDEO: read [0x%llx]\n", addr);
    return 0;
}
static void nv2a_pvideo_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    NV2A_DPRINTF("nv2a PVIDEO: [0x%llx] = 0x%02llx\n", addr, val);
}




/* PIMTER - time measurement and time-based alarms */
static uint64_t nv2a_ptimer_get_clock(NV2AState *d)
{
    return muldiv64(qemu_get_clock_ns(vm_clock),
                    d->pramdac.core_clock_freq * d->ptimer.numerator,
                    get_ticks_per_sec() * d->ptimer.denominator);
}
static uint64_t nv2a_ptimer_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    NV2AState *d = opaque;

    uint64_t r = 0;
    switch (addr) {
    case NV_PTIMER_INTR_0:
        r = d->ptimer.pending_interrupts;
        break;
    case NV_PTIMER_INTR_EN_0:
        r = d->ptimer.enabled_interrupts;
        break;
    case NV_PTIMER_NUMERATOR:
        r = d->ptimer.numerator;
        break;
    case NV_PTIMER_DENOMINATOR:
        r = d->ptimer.denominator;
        break;
    case NV_PTIMER_TIME_0:
        r = (nv2a_ptimer_get_clock(d) & 0x7ffffff) << 5;
        break;
    case NV_PTIMER_TIME_1:
        r = (nv2a_ptimer_get_clock(d) >> 27) & 0x1fffffff;
        break;
    default:
        break;
    }

    NV2A_DPRINTF("nv2a PTIMER: read [0x%llx] -> 0x%llx\n", addr, r);
    return r;
}
static void nv2a_ptimer_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    NV2AState *d = opaque;

    NV2A_DPRINTF("nv2a PTIMER: [0x%llx] = 0x%02llx\n", addr, val);

    switch (addr) {
    case NV_PTIMER_INTR_0:
        d->ptimer.pending_interrupts &= ~val;
        nv2a_update_irq(d);
        break;
    case NV_PTIMER_INTR_EN_0:
        d->ptimer.enabled_interrupts = val;
        nv2a_update_irq(d);
        break;
    case NV_PTIMER_DENOMINATOR:
        d->ptimer.denominator = val;
        break;
    case NV_PTIMER_NUMERATOR:
        d->ptimer.numerator = val;
        break;
    case NV_PTIMER_ALARM_0:
        d->ptimer.alarm_time = val;
        break;
    default:
        break;
    }
}


static uint64_t nv2a_pcounter_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    NV2A_DPRINTF("nv2a PCOUNTER: read [0x%llx]\n", addr);
    return 0;
}
static void nv2a_pcounter_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    NV2A_DPRINTF("nv2a PCOUNTER: [0x%llx] = 0x%02llx\n", addr, val);
}


static uint64_t nv2a_pvpe_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    NV2A_DPRINTF("nv2a PVPE: read [0x%llx]\n", addr);
    return 0;
}
static void nv2a_pvpe_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    NV2A_DPRINTF("nv2a PVPE: [0x%llx] = 0x%02llx\n", addr, val);
}


static uint64_t nv2a_ptv_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    NV2A_DPRINTF("nv2a PTV: read [0x%llx]\n", addr);
    return 0;
}
static void nv2a_ptv_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    NV2A_DPRINTF("nv2a PTV: [0x%llx] = 0x%02llx\n", addr, val);
}


static uint64_t nv2a_prmfb_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    NV2A_DPRINTF("nv2a PRMFB: read [0x%llx]\n", addr);
    return 0;
}
static void nv2a_prmfb_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    NV2A_DPRINTF("nv2a PRMFB: [0x%llx] = 0x%02llx\n", addr, val);
}


/* PRMVIO - aliases VGA sequencer and graphics controller registers */
static uint64_t nv2a_prmvio_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    NV2AState *d = opaque;
    uint64_t r = vga_ioport_read(&d->vga, addr);

    NV2A_DPRINTF("nv2a PRMVIO: read [0x%llx] -> 0x%llx\n", addr, r);
    return r;
}
static void nv2a_prmvio_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    NV2AState *d = opaque;

    NV2A_DPRINTF("nv2a PRMVIO: [0x%llx] = 0x%02llx\n", addr, val);

    vga_ioport_write(&d->vga, addr, val);
}


static uint64_t nv2a_pfb_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    NV2AState *d = opaque;

    uint64_t r = 0;
    switch (addr) {
    case NV_PFB_CFG0:
        /* 3-4 memory partitions. The debug bios checks this. */
        r = 3;
        break;
    case NV_PFB_CSTATUS:
        r = memory_region_size(&d->vram);
        break;
    default:
        break;
    }

    NV2A_DPRINTF("nv2a PFB: read [0x%llx] -> 0x%llx\n", addr, r);
    return r;
}
static void nv2a_pfb_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    NV2A_DPRINTF("nv2a PFB: [0x%llx] = 0x%02llx\n", addr, val);
}


static uint64_t nv2a_pstraps_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    NV2A_DPRINTF("nv2a PSTRAPS: read [0x%llx]\n", addr);
    return 0;
}
static void nv2a_pstraps_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    NV2A_DPRINTF("nv2a PSTRAPS: [0x%llx] = 0x%02llx\n", addr, val);
}

/* PGRAPH - accelerated 2d/3d drawing engine */
static uint64_t nv2a_pgraph_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    NV2AState *d = opaque;

    uint64_t r = 0;
    switch (addr) {
    case NV_PGRAPH_CTX_USER:
        r = d->pgraph.context[d->pgraph.channel_id].channel_3d
            | NV_PGRAPH_CTX_USER_CHANNEL_3D_VALID
            | (d->pgraph.context[d->pgraph.channel_id].subchannel << 13)
            | (d->pgraph.channel_id << 24);
        break;
    case NV_PGRAPH_CHANNEL_CTX_TABLE:
        r = d->pgraph.context_table;
        break;
    case NV_PGRAPH_CHANNEL_CTX_POINTER:
        r = d->pgraph.context_pointer;
        break;
    default:
        break;
    }

    NV2A_DPRINTF("nv2a PGRAPH: read [0x%llx]\n", addr);
    return r;
}
static void nv2a_pgraph_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    NV2AState *d = opaque;

    NV2A_DPRINTF("nv2a PGRAPH: [0x%llx] = 0x%02llx\n", addr, val);

    switch (addr) {
    case NV_PGRAPH_CTX_USER:
        d->pgraph.channel_id = (val & NV_PGRAPH_CTX_USER_CHID) >> 24;
        d->pgraph.context[d->pgraph.channel_id].channel_id =
            d->pgraph.channel_id;
        d->pgraph.context[d->pgraph.channel_id].channel_3d =
            val & NV_PGRAPH_CTX_USER_CHANNEL_3D;
        d->pgraph.context[d->pgraph.channel_id].subchannel =
            (val & NV_PGRAPH_CTX_USER_SUBCH) >> 13;

        break;
    case NV_PGRAPH_CHANNEL_CTX_TABLE:
        d->pgraph.context_table = val & NV_PGRAPH_CHANNEL_CTX_TABLE_INST;
        break;
    case NV_PGRAPH_CHANNEL_CTX_POINTER:
        d->pgraph.context_pointer = val & NV_PGRAPH_CHANNEL_CTX_POINTER_INST;
        break;
    case NV_PGRAPH_CHANNEL_CTX_TRIGGER:
        if (val & NV_PGRAPH_CHANNEL_CTX_TRIGGER_READ_IN) {
            /* do stuff ... */
        }
        if (val & NV_PGRAPH_CHANNEL_CTX_TRIGGER_WRITE_OUT) {
            /* do stuff ... */
        }
        break;
    default:
        break;
    }
}


static uint64_t nv2a_pcrtc_read(void *opaque,
                                hwaddr addr, unsigned int size)
{
    NV2AState *d = opaque;

    uint64_t r = 0;
    switch (addr) {
        case NV_PCRTC_INTR_0:
            r = d->pcrtc.pending_interrupts;
            break;
        case NV_PCRTC_INTR_EN_0:
            r = d->pcrtc.enabled_interrupts;
            break;
        case NV_PCRTC_START:
            r = d->pcrtc.start;
            break;
        default:
            break;
    }

    NV2A_DPRINTF("nv2a PCRTC: read [0x%llx] -> 0x%llx\n", addr, r);
    return r;
}
static void nv2a_pcrtc_write(void *opaque, hwaddr addr,
                             uint64_t val, unsigned int size)
{
    NV2AState *d = opaque;

    NV2A_DPRINTF("nv2a PCRTC: [0x%llx] = 0x%02llx\n", addr, val);

    switch (addr) {
    case NV_PCRTC_INTR_0:
        d->pcrtc.pending_interrupts &= ~val;
        nv2a_update_irq(d);
        break;
    case NV_PCRTC_INTR_EN_0:
        d->pcrtc.enabled_interrupts = val;
        nv2a_update_irq(d);
        break;
    case NV_PCRTC_START:
        val &= 0x03FFFFFF;
        if (val != d->pcrtc.start) {
            if (d->pcrtc.start) {
                memory_region_del_subregion(&d->vram, &d->vga.vram);
            }
            d->pcrtc.start = val;
            memory_region_add_subregion(&d->vram, val, &d->vga.vram);
        }
        break;
    default:
        break;
    }
}


/* PRMCIO - aliases VGA CRTC and attribute controller registers */
static uint64_t nv2a_prmcio_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    NV2AState *d = opaque;
    uint64_t r = vga_ioport_read(&d->vga, addr);

    NV2A_DPRINTF("nv2a PRMCIO: read [0x%llx] -> 0x%llx\n", addr, r);
    return r;
}
static void nv2a_prmcio_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    NV2AState *d = opaque;

    NV2A_DPRINTF("nv2a PRMCIO: [0x%llx] = 0x%02llx\n", addr, val);

    switch (addr) {
    case VGA_ATT_W:
        /* Cromwell sets attrs without enabling VGA_AR_ENABLE_DISPLAY
         * (which should result in a blank screen).
         * Either nvidia's hardware is lenient or it is set through
         * something else. The former seems more likely.
         */
        if (d->vga.ar_flip_flop == 0) {
            val |= VGA_AR_ENABLE_DISPLAY;
        }
        break;
    default:
        break;
    }

    vga_ioport_write(&d->vga, addr, val);
}


static uint64_t nv2a_pramdac_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    NV2AState *d = opaque;

    uint64_t r = 0;
    switch (addr & ~3) {
    case NV_PRAMDAC_NVPLL_COEFF:
        r = d->pramdac.core_clock_coeff;
        break;
    case NV_PRAMDAC_MPLL_COEFF:
        r = d->pramdac.memory_clock_coeff;
        break;
    case NV_PRAMDAC_VPLL_COEFF:
        r = d->pramdac.video_clock_coeff;
        break;
    case NV_PRAMDAC_PLL_TEST_COUNTER:
        /* emulated PLLs locked instantly? */
        return NV_PRAMDAC_PLL_TEST_COUNTER_VPLL2_LOCK
             | NV_PRAMDAC_PLL_TEST_COUNTER_NVPLL_LOCK
             | NV_PRAMDAC_PLL_TEST_COUNTER_MPLL_LOCK
             | NV_PRAMDAC_PLL_TEST_COUNTER_VPLL_LOCK;
    default:
        break;
    }

    /* Surprisingly, QEMU doesn't handle unaligned access for you properly */
    r >>= 32 - 8 * size - 8 * (addr & 3);

    NV2A_DPRINTF("nv2a PRAMDAC: read %d [0x%llx] -> %llx\n", size, addr, r);
    return r;
}
static void nv2a_pramdac_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    NV2AState *d = opaque;
    uint32_t m, n, p;

    NV2A_DPRINTF("nv2a PRAMDAC: [0x%llx] = 0x%02llx\n", addr, val);

    switch (addr) {
    case NV_PRAMDAC_NVPLL_COEFF:
        d->pramdac.core_clock_coeff = val;

        m = val & NV_PRAMDAC_NVPLL_COEFF_MDIV;
        n = (val & NV_PRAMDAC_NVPLL_COEFF_NDIV) >> 8;
        p = (val & NV_PRAMDAC_NVPLL_COEFF_PDIV) >> 16;

        if (m == 0) {
            d->pramdac.core_clock_freq = 0;
        } else {
            d->pramdac.core_clock_freq = (NV2A_CRYSTAL_FREQ * n)
                                          / (1 << p) / m;
        }

        break;
    case NV_PRAMDAC_MPLL_COEFF:
        d->pramdac.memory_clock_coeff = val;
        break;
    case NV_PRAMDAC_VPLL_COEFF:
        d->pramdac.video_clock_coeff = val;
        break;
    default:
        break;
    }
}


static uint64_t nv2a_prmdio_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    NV2A_DPRINTF("nv2a PRMDIO: read [0x%llx]\n", addr);
    return 0;
}
static void nv2a_prmdio_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    NV2A_DPRINTF("nv2a PRMDIO: [0x%llx] = 0x%02llx\n", addr, val);
}


/* PRAMIN - RAMIN access */
/*
static uint64_t nv2a_pramin_read(void *opaque,
                                 hwaddr addr, unsigned int size)
{
    NV2A_DPRINTF("nv2a PRAMIN: read [0x%llx] -> 0x%llx\n", addr, r);
    return 0;
}
static void nv2a_pramin_write(void *opaque, hwaddr addr,
                              uint64_t val, unsigned int size)
{
    NV2A_DPRINTF("nv2a PRAMIN: [0x%llx] = 0x%02llx\n", addr, val);
}*/


/* USER - PFIFO MMIO and DMA submission area */
static uint64_t nv2a_user_read(void *opaque,
                               hwaddr addr, unsigned int size)
{
    NV2AState *d = opaque;

    unsigned int channel_id = addr >> 16;
    assert(channel_id < NV2A_NUM_CHANNELS);

    ChannelControl *control = &d->user.channel_control[channel_id];

    uint64_t r = 0;
    if (d->pfifo.channel_modes & (1 << channel_id)) {
        /* DMA Mode */
        switch (addr & 0xFFFF) {
        case NV_USER_DMA_PUT:
            r = control->dma_put;
            break;
        case NV_USER_DMA_GET:
            r = control->dma_get;
            break;
        case NV_USER_REF:
            r = control->ref;
            break;
        default:
            break;
        }
    } else {
        /* PIO Mode */
        /* dunno */
    }

    NV2A_DPRINTF("nv2a USER: read [0x%llx] -> %llx\n", addr, r);
    return r;
}
static void nv2a_user_write(void *opaque, hwaddr addr,
                            uint64_t val, unsigned int size)
{
    NV2AState *d = opaque;

    NV2A_DPRINTF("nv2a USER: [0x%llx] = 0x%02llx\n", addr, val);

    unsigned int channel_id = addr >> 16;
    assert(channel_id < NV2A_NUM_CHANNELS);

    ChannelControl *control = &d->user.channel_control[channel_id];

    if (d->pfifo.channel_modes & (1 << channel_id)) {
        /* DMA Mode */
        switch (addr & 0xFFFF) {
        case NV_USER_DMA_PUT:
            control->dma_put = val;

            if (d->pfifo.cache1.push_enabled) {
                nv2a_run_pusher(d);
            }
            break;
        case NV_USER_DMA_GET:
            control->dma_get = val;
            break;
        case NV_USER_REF:
            control->ref = val;
            break;
        default:
            break;
        }
    } else {
        /* PIO Mode */
        /* dunno */
    }

}




typedef struct NV2ABlockInfo {
    const char* name;
    hwaddr offset;
    uint64_t size;
    MemoryRegionOps ops;
} NV2ABlockInfo;

static const struct NV2ABlockInfo blocktable[] = {
    [ NV_PMC ]  = {
        .name = "PMC",
        .offset = 0x000000,
        .size   = 0x001000,
        .ops = {
            .read = nv2a_pmc_read,
            .write = nv2a_pmc_write,
        },
    },
    [ NV_PBUS ]  = {
        .name = "PBUS",
        .offset = 0x001000,
        .size   = 0x001000,
        .ops = {
            .read = nv2a_pbus_read,
            .write = nv2a_pbus_write,
        },
    },
    [ NV_PFIFO ]  = {
        .name = "PFIFO",
        .offset = 0x002000,
        .size   = 0x002000,
        .ops = {
            .read = nv2a_pfifo_read,
            .write = nv2a_pfifo_write,
        },
    },
    [ NV_PRMA ]  = {
        .name = "PRMA",
        .offset = 0x007000,
        .size   = 0x001000,
        .ops = {
            .read = nv2a_prma_read,
            .write = nv2a_prma_write,
        },
    },
    [ NV_PVIDEO ]  = {
        .name = "PVIDEO",
        .offset = 0x008000,
        .size   = 0x001000,
        .ops = {
            .read = nv2a_pvideo_read,
            .write = nv2a_pvideo_write,
        },
    },
    [ NV_PTIMER ]  = {
        .name = "PTIMER",
        .offset = 0x009000,
        .size   = 0x001000,
        .ops = {
            .read = nv2a_ptimer_read,
            .write = nv2a_ptimer_write,
        },
    },
    [ NV_PCOUNTER ]  = {
        .name = "PCOUNTER",
        .offset = 0x00a000,
        .size   = 0x001000,
        .ops = {
            .read = nv2a_pcounter_read,
            .write = nv2a_pcounter_write,
        },
    },
    [ NV_PVPE ]  = {
        .name = "PVPE",
        .offset = 0x00b000,
        .size   = 0x001000,
        .ops = {
            .read = nv2a_pvpe_read,
            .write = nv2a_pvpe_write,
        },
    },
    [ NV_PTV ]  = {
        .name = "PTV",
        .offset = 0x00d000,
        .size   = 0x001000,
        .ops = {
            .read = nv2a_ptv_read,
            .write = nv2a_ptv_write,
        },
    },
    [ NV_PRMFB ]  = {
        .name = "PRMFB",
        .offset = 0x0a0000,
        .size   = 0x020000,
        .ops = {
            .read = nv2a_prmfb_read,
            .write = nv2a_prmfb_write,
        },
    },
    [ NV_PRMVIO ]  = {
        .name = "PRMVIO",
        .offset = 0x0c0000,
        .size   = 0x001000,
        .ops = {
            .read = nv2a_prmvio_read,
            .write = nv2a_prmvio_write,
        },
    },
    [ NV_PFB ]  = {
        .name = "PFB",
        .offset = 0x100000,
        .size   = 0x001000,
        .ops = {
            .read = nv2a_pfb_read,
            .write = nv2a_pfb_write,
        },
    },
    [ NV_PSTRAPS ]  = {
        .name = "PSTRAPS",
        .offset = 0x101000,
        .size   = 0x001000,
        .ops = {
            .read = nv2a_pstraps_read,
            .write = nv2a_pstraps_write,
        },
    },
    [ NV_PGRAPH ]  = {
        .name = "PGRAPH",
        .offset = 0x400000,
        .size   = 0x002000,
        .ops = {
            .read = nv2a_pgraph_read,
            .write = nv2a_pgraph_write,
        },
    },
    [ NV_PCRTC ]  = {
        .name = "PCRTC",
        .offset = 0x600000,
        .size   = 0x001000,
        .ops = {
            .read = nv2a_pcrtc_read,
            .write = nv2a_pcrtc_write,
        },
    },
    [ NV_PRMCIO ]  = {
        .name = "PRMCIO",
        .offset = 0x601000,
        .size   = 0x001000,
        .ops = {
            .read = nv2a_prmcio_read,
            .write = nv2a_prmcio_write,
        },
    },
    [ NV_PRAMDAC ]  = {
        .name = "PRAMDAC",
        .offset = 0x680000,
        .size   = 0x001000,
        .ops = {
            .read = nv2a_pramdac_read,
            .write = nv2a_pramdac_write,
        },
    },
    [ NV_PRMDIO ]  = {
        .name = "PRMDIO",
        .offset = 0x681000,
        .size   = 0x001000,
        .ops = {
            .read = nv2a_prmdio_read,
            .write = nv2a_prmdio_write,
        },
    },
    /*[ NV_PRAMIN ]  = {
        .name = "PRAMIN",
        .offset = 0x700000,
        .size   = 0x100000,
        .ops = {
            .read = nv2a_pramin_read,
            .write = nv2a_pramin_write,
        },
    },*/
    [ NV_USER ]  = {
        .name = "USER",
        .offset = 0x800000,
        .size   = 0x800000,
        .ops = {
            .read = nv2a_user_read,
            .write = nv2a_user_write,
        },
    },
};



static int nv2a_get_bpp(VGACommonState *s)
{
    if ((s->cr[0x28] & 3) == 3) {
        return 32;
    }
    return (s->cr[0x28] & 3) * 8;
}


/* Graphic console methods. Need to wrap all of these since
 * graphic_console_init takes a single opaque, and we
 * need access to the nv2a state to set the vblank interrupt */
static void nv2a_vga_update(void *opaque)
{
    NV2AState *d = NV2A_DEVICE(opaque);
    d->vga.update(&d->vga);

    d->pcrtc.pending_interrupts |= NV_PCRTC_INTR_0_VBLANK;
    nv2a_update_irq(d);
}
static void nv2a_vga_invalidate(void *opaque)
{
    NV2AState *d = NV2A_DEVICE(opaque);
    d->vga.invalidate(&d->vga);
}
static void nv2a_vga_screen_dump(void *opaque,
                                 const char *filename,
                                 bool cswitch,
                                 Error **errp)
{
    NV2AState *d = NV2A_DEVICE(opaque);
    d->vga.screen_dump(&d->vga, filename, cswitch, errp);
}
static void nv2a_vga_text_update(void *opaque, console_ch_t *chardata)
{
    NV2AState *d = NV2A_DEVICE(opaque);
    d->vga.text_update(&d->vga, chardata);
}

static int nv2a_initfn(PCIDevice *dev)
{
    int i;
    NV2AState *d;

    d = NV2A_DEVICE(dev);

    d->pcrtc.start = 0;

    d->pramdac.core_clock_coeff = 0x00011c01; /* 189MHz...? */
    d->pramdac.core_clock_freq = 189000000;
    d->pramdac.memory_clock_coeff = 0;
    d->pramdac.video_clock_coeff = 0x0003C20D; /* 25182Khz...? */



    /* legacy VGA shit */
    VGACommonState *vga = &d->vga;
    vga->vram_size_mb = 16;
    /* seems to start in color mode */
    vga->msr = VGA_MIS_COLOR;

    vga_common_init(vga);
    vga->get_bpp = nv2a_get_bpp;

    vga->ds = graphic_console_init(nv2a_vga_update,
                                   nv2a_vga_invalidate,
                                   nv2a_vga_screen_dump,
                                   nv2a_vga_text_update,
                                   d);


    /* mmio */
    memory_region_init(&d->mmio, "nv2a-mmio", 0x1000000);
    pci_register_bar(&d->dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &d->mmio);

    for (i=0; i<sizeof(blocktable)/sizeof(blocktable[0]); i++) {
        if (!blocktable[i].name) continue;
        memory_region_init_io(&d->block_mmio[i], &blocktable[i].ops, d,
                              blocktable[i].name, blocktable[i].size);
        memory_region_add_subregion(&d->mmio, blocktable[i].offset,
                                    &d->block_mmio[i]);
    }


    /* vram */
    memory_region_init_ram(&d->vram, "nv2a-vram", 128 * 0x100000);
    pci_register_bar(&d->dev, 1, PCI_BASE_ADDRESS_MEM_PREFETCH, &d->vram);

    memory_region_init_alias(&d->ramin, "nv2a-ramin", &d->vram,
                             memory_region_size(&d->vram) - 0x100000,
                             0x100000);
    memory_region_add_subregion(&d->mmio, 0x700000, &d->ramin);

    d->ramin_ptr = memory_region_get_ram_ptr(&d->ramin);

    return 0;
}

static void nv2a_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->vendor_id = PCI_VENDOR_ID_NVIDIA;
    k->device_id = PCI_DEVICE_ID_NVIDIA_GEFORCE_NV2A;
    k->revision = 161;
    k->class_id = PCI_CLASS_DISPLAY_3D;
    k->init = nv2a_initfn;

    dc->desc = "GeForce NV2A Integrated Graphics";
}

static const TypeInfo nv2a_info = {
    .name          = "nv2a",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(NV2AState),
    .class_init    = nv2a_class_init,
};

static void nv2a_register(void)
{
    type_register_static(&nv2a_info);
}
type_init(nv2a_register);





void nv2a_init(PCIBus *bus, int devfn, qemu_irq irq)
{
    PCIDevice *dev;
    NV2AState *d;
    dev = pci_create_simple(bus, devfn, "nv2a");
    d = NV2A_DEVICE(dev);
    d->irq = irq;
}