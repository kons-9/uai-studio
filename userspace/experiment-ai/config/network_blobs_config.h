#ifndef AI_NETWORK_BLOBS_CONFIG_H
#define AI_NETWORK_BLOBS_CONFIG_H

/* Keep the generated Neural-ART epoch-controller blobs in the dedicated
 * memory-mapped NOR section selected by the experiment-ai linker script. */
#define ECBLOB_CONST_SECTION __attribute__((section(".network_blobs")))

#endif
