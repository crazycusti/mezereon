#ifndef CONFIG_H
#define CONFIG_H

// Netzwerk-Parameter für NE2000, bitte anpassen Standardwerte sind 0x300 und IRQ 3 oder 5!
#define CONFIG_NE2000_IO   0x300
#define CONFIG_NE2000_IRQ  3
#define CONFIG_NE2000_IO_SIZE 32

// Video params für Zeilenauflösung
#define CONFIG_VGA_WIDTH 80
#define CONFIG_VGA_HEIGHT 25

#endif // CONFIG_H
