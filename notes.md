TODO:

- setup SD card driver
- Implement SD card write
- Implement SD card thread write from fifo
- setup SPI intan overlay
- setup intan driver
- implement intan read

Potentially:

- potentially neural network to fill in the gaps where there might be missing packets

Done:

- Implement FIFO queue with faketada module
- implement logic to renegotiate comms terms at the beginning to go for low latency (look at devacademy)
- understand setting up constant connection with regular data windows, why is it not like that already?
- do we need notifications for this to be the case?
- understand devicetree overlays
- setup SD card device tree overlay
