TODO:

- implement sem to 100 for preventing fifo fillup
- setup SPI intan overlay
- setup intan driver
- implement intan read

Potentially:

- potentially neural network to fill in the gaps where there might be missing packets

Done:

- setup SD card driver
- make sure the prj conf works and is sufficient
- Implement FIFO queue with faketada module
- implement logic to renegotiate comms terms at the beginning to go for low latency (look at devacademy)
- understand setting up constant connection with regular data windows, why is it not like that already?
- do we need notifications for this to be the case?
- understand devicetree overlays
- setup SD card device tree overlay
- Implement SD card write
- Implement SD card thread write from fifo
