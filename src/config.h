/*
 * alttab - Alt+Tab window switcher
 * config.h - Command-line configuration
 */

#ifndef ALTTAB_CONFIG_H
#define ALTTAB_CONFIG_H

#include <stdbool.h>
#include <X11/X.h>

/* Version information */
#define ALTTAB_VERSION "0.1.0"

/* RGBA color struct */
typedef struct {
    double r;
    double g;
    double b;
    double a;
} ColorRGBA;

/* Configuration struct holding all runtime settings */
typedef struct Config {
    /* Keybindings */
    const char *key_name;       /* Key name (default: "Tab") */
    const char *mod_name;       /* Modifier name (default: "Mod1") */
    unsigned int key_sym;       /* X KeySym for trigger key */
    unsigned int mod_mask;      /* X modifier mask */

    /* Colors */
    ColorRGBA bg_color;         /* Overlay background color */
    ColorRGBA highlight_color;  /* Selection highlight color */

    /* Tile dimensions */
    int tile_width;             /* Base tile width in pixels */
    int tile_height;            /* Base tile height in pixels */

    /* Feature toggles */
    bool no_thumbnails;         /* Disable thumbnail capture */
    bool verbose;               /* Enable verbose/debug logging */
} Config;

/* Global configuration instance */
extern Config g_config;

/*
 * Parse command-line arguments and populate g_config.
 * Returns 0 on success, 1 if --help/--version was shown (exit normally),
 * or -1 on error (exit with error).
 */
int config_parse(int argc, char *argv[]);

/*
 * Initialize configuration with default values.
 */
void config_init_defaults(void);

/*
 * Parse an RGBA color string.
 * Accepts formats: "r,g,b,a" (0.0-1.0 floats) or "#RRGGBBAA" (hex)
 * Returns true on success.
 */
bool config_parse_color(const char *str, ColorRGBA *color);

/*
 * Parse a key name to KeySym.
 * Returns KeySym or NoSymbol on error.
 */
unsigned int config_parse_key(const char *name);

/*
 * Parse a modifier name to X modifier mask.
 * Accepts: Mod1, Alt, Mod4, Super, Shift, Control
 * Returns modifier mask or 0 on error.
 */
unsigned int config_parse_mod(const char *name);

#endif /* ALTTAB_CONFIG_H */
