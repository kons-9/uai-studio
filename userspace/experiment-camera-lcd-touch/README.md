# experiment-camera-lcd-touch

This STM32N6570-DK experiment shows the IMX335 camera preview and a touch driven button UI. It
uses the board's GT911 touch controller and the STM32CubeN6 display and camera BSP.

On first use, generate the CubeMX support sources and build:

```sh
make -C userspace/experiment-camera-lcd-touch setup
make -C userspace/experiment-camera-lcd-touch build
```

Open the UART monitor before loading the RAM image:

```sh
make -C userspace/experiment-camera-lcd-touch monitor
```

In another terminal, run:

```sh
make -C userspace/experiment-camera-lcd-touch ram-run
```

The display has four buttons. `RED`, `GREEN`, and `BLUE` select the color used by the image marker;
`CLEAR` removes the marker. Touching the camera image places the marker at that position. UART logs
report each button action and the sampled display coordinates.

## Display composition policy

- Keep the DCMIPP capture frame and two LCD display pages in separate RGB565 buffers. Compose into
  the back page, then switch the LTDC address during vertical blanking. This follows the working
  detection-box rendering path and prevents the LCD from scanning a page while the CPU rewrites it.
- Use one LTDC layer for the composed frame. This avoids relying on the second layer's alpha blend
  behavior on the board while keeping the controls readable over moving video.
- Store each button's label, action, color, and hit rectangle in one static table. Drawing and touch
  hit testing both use that table and the same 800×480 display coordinate space.
- Poll GT911 in the application task and dispatch only the transition from released to pressed.
  This makes one physical tap one button action and avoids doing display work in the touch interrupt.
- Rebuild the back page from the latest camera capture, draw the UI, then clean its data cache before
  requesting a vertical-blanking page flip. Keep future widgets in the same renderer and give
  overlapping controls an explicit draw and hit-test order.
- Keep touch input, button/action routing, camera capture, display composition, and pixel drawing in
  separate drivers. Multi-touch and keyboard focus can be added without changing camera capture.

The two display pages are reserved at `0x34030000` and `0x34100000`. The 32 KiB camera task stack
is at `0x341C0000`, separate from both display pages and the camera capture frame in SRAM3/4. The
CubeMX clock and external-memory setup comes from this experiment's local
`config/stm32n6570-dk-fullsecure.ioc`.
