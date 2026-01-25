/*
 * alttab - Alt+Tab window switcher
 * config.c - Command-line configuration parsing
 */

#define _GNU_SOURCE  /* For strcasecmp */

#include "config.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <X11/keysym.h>

/* Default colors (same as overlay.c defaults) */
#define DEFAULT_BG_R          0.1
#define DEFAULT_BG_G          0.1
#define DEFAULT_BG_B          0.1
#define DEFAULT_BG_A          0.85

#define DEFAULT_HIGHLIGHT_R   0.3
#define DEFAULT_HIGHLIGHT_G   0.5
#define DEFAULT_HIGHLIGHT_B   0.8
#define DEFAULT_HIGHLIGHT_A   0.9

/* Default tile dimensions (same as layout.h defaults) */
#define DEFAULT_TILE_WIDTH    200
#define DEFAULT_TILE_HEIGHT   150

/* Global configuration instance */
Config g_config;

void config_init_defaults(void)
{
    /* Keybindings */
    g_config.key_name = "Tab";
    g_config.mod_name = "Mod1";
    g_config.key_sym = XK_Tab;
    g_config.mod_mask = Mod1Mask;

    /* Colors */
    g_config.bg_color.r = DEFAULT_BG_R;
    g_config.bg_color.g = DEFAULT_BG_G;
    g_config.bg_color.b = DEFAULT_BG_B;
    g_config.bg_color.a = DEFAULT_BG_A;

    g_config.highlight_color.r = DEFAULT_HIGHLIGHT_R;
    g_config.highlight_color.g = DEFAULT_HIGHLIGHT_G;
    g_config.highlight_color.b = DEFAULT_HIGHLIGHT_B;
    g_config.highlight_color.a = DEFAULT_HIGHLIGHT_A;

    /* Tile dimensions */
    g_config.tile_width = DEFAULT_TILE_WIDTH;
    g_config.tile_height = DEFAULT_TILE_HEIGHT;

    /* Feature toggles */
    g_config.no_thumbnails = false;
    g_config.verbose = false;
}

bool config_parse_color(const char *str, ColorRGBA *color)
{
    if (!str || !color)
        return false;

    /* Try hex format: #RRGGBBAA or #RRGGBB */
    if (str[0] == '#') {
        unsigned int r, g, b, a = 255;
        int len = strlen(str);

        if (len == 9) {
            /* #RRGGBBAA */
            if (sscanf(str, "#%2x%2x%2x%2x", &r, &g, &b, &a) != 4)
                return false;
        } else if (len == 7) {
            /* #RRGGBB */
            if (sscanf(str, "#%2x%2x%2x", &r, &g, &b) != 3)
                return false;
        } else {
            return false;
        }

        color->r = r / 255.0;
        color->g = g / 255.0;
        color->b = b / 255.0;
        color->a = a / 255.0;
        return true;
    }

    /* Try float format: r,g,b,a or r,g,b */
    double r, g, b, a = 1.0;
    int n = sscanf(str, "%lf,%lf,%lf,%lf", &r, &g, &b, &a);
    if (n >= 3) {
        if (r < 0.0 || r > 1.0 || g < 0.0 || g > 1.0 ||
            b < 0.0 || b > 1.0 || a < 0.0 || a > 1.0)
            return false;

        color->r = r;
        color->g = g;
        color->b = b;
        color->a = a;
        return true;
    }

    return false;
}

unsigned int config_parse_key(const char *name)
{
    if (!name)
        return NoSymbol;

    /* XStringToKeysym handles standard key names */
    KeySym sym = XStringToKeysym(name);
    return sym;
}

unsigned int config_parse_mod(const char *name)
{
    if (!name)
        return 0;

    /* Case-insensitive comparison */
    if (strcasecmp(name, "Mod1") == 0 || strcasecmp(name, "Alt") == 0)
        return Mod1Mask;
    if (strcasecmp(name, "Mod2") == 0)
        return Mod2Mask;
    if (strcasecmp(name, "Mod3") == 0)
        return Mod3Mask;
    if (strcasecmp(name, "Mod4") == 0 || strcasecmp(name, "Super") == 0)
        return Mod4Mask;
    if (strcasecmp(name, "Mod5") == 0)
        return Mod5Mask;
    if (strcasecmp(name, "Shift") == 0)
        return ShiftMask;
    if (strcasecmp(name, "Control") == 0 || strcasecmp(name, "Ctrl") == 0)
        return ControlMask;

    return 0;
}

static void print_help(const char *prog_name)
{
    printf("Usage: %s [options]\n", prog_name);
    printf("\n");
    printf("Alt+Tab window switcher for X11.\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help                 Show this help message and exit\n");
    printf("  -V, --version              Show version information and exit\n");
    printf("  -k, --key=KEY              Trigger key (default: Tab)\n");
    printf("  -m, --mod=MOD              Modifier key (default: Mod1/Alt)\n");
    printf("                             Valid: Mod1, Alt, Mod4, Super, Shift, Control\n");
    printf("  -b, --bg-color=RGBA        Background color (default: #1a1a1ad9)\n");
    printf("  -s, --highlight-color=RGBA Selection highlight color (default: #4d80cce6)\n");
    printf("                             Color format: #RRGGBBAA or r,g,b,a (0.0-1.0)\n");
    printf("  -W, --tile-width=PX        Base tile width in pixels (default: 200)\n");
    printf("  -H, --tile-height=PX       Base tile height in pixels (default: 150)\n");
    printf("  -T, --no-thumbnails        Disable window thumbnail capture\n");
    printf("  -v, --verbose              Enable debug logging\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s --mod=Super              Use Super+Tab instead of Alt+Tab\n", prog_name);
    printf("  %s --bg-color=#000000cc    Use darker background\n", prog_name);
    printf("  %s --tile-width=250        Use wider tiles\n", prog_name);
}

static void print_version(void)
{
    printf("alttab %s\n", ALTTAB_VERSION);
}

int config_parse(int argc, char *argv[])
{
    /* Initialize defaults first */
    config_init_defaults();

    static struct option long_options[] = {
        {"help",            no_argument,       NULL, 'h'},
        {"version",         no_argument,       NULL, 'V'},
        {"key",             required_argument, NULL, 'k'},
        {"mod",             required_argument, NULL, 'm'},
        {"bg-color",        required_argument, NULL, 'b'},
        {"highlight-color", required_argument, NULL, 's'},
        {"tile-width",      required_argument, NULL, 'W'},
        {"tile-height",     required_argument, NULL, 'H'},
        {"no-thumbnails",   no_argument,       NULL, 'T'},
        {"verbose",         no_argument,       NULL, 'v'},
        {NULL, 0, NULL, 0}
    };

    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "hVk:m:b:s:W:H:Tv",
                              long_options, &option_index)) != -1) {
        switch (opt) {
        case 'h':
            print_help(argv[0]);
            return 1;

        case 'V':
            print_version();
            return 1;

        case 'k':
            g_config.key_sym = config_parse_key(optarg);
            if (g_config.key_sym == NoSymbol) {
                fprintf(stderr, "error: invalid key name: %s\n", optarg);
                return -1;
            }
            g_config.key_name = optarg;
            break;

        case 'm':
            g_config.mod_mask = config_parse_mod(optarg);
            if (g_config.mod_mask == 0) {
                fprintf(stderr, "error: invalid modifier: %s\n", optarg);
                fprintf(stderr, "Valid modifiers: Mod1, Alt, Mod4, Super, Shift, Control\n");
                return -1;
            }
            g_config.mod_name = optarg;
            break;

        case 'b':
            if (!config_parse_color(optarg, &g_config.bg_color)) {
                fprintf(stderr, "error: invalid color format: %s\n", optarg);
                fprintf(stderr, "Use #RRGGBBAA or r,g,b,a (0.0-1.0)\n");
                return -1;
            }
            break;

        case 's':
            if (!config_parse_color(optarg, &g_config.highlight_color)) {
                fprintf(stderr, "error: invalid color format: %s\n", optarg);
                fprintf(stderr, "Use #RRGGBBAA or r,g,b,a (0.0-1.0)\n");
                return -1;
            }
            break;

        case 'W':
            g_config.tile_width = atoi(optarg);
            if (g_config.tile_width < 50 || g_config.tile_width > 1000) {
                fprintf(stderr, "error: tile width must be between 50 and 1000\n");
                return -1;
            }
            break;

        case 'H':
            g_config.tile_height = atoi(optarg);
            if (g_config.tile_height < 50 || g_config.tile_height > 1000) {
                fprintf(stderr, "error: tile height must be between 50 and 1000\n");
                return -1;
            }
            break;

        case 'T':
            g_config.no_thumbnails = true;
            break;

        case 'v':
            g_config.verbose = true;
            break;

        case '?':
            /* getopt_long already printed an error message */
            fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
            return -1;

        default:
            return -1;
        }
    }

    /* Check for unexpected positional arguments */
    if (optind < argc) {
        fprintf(stderr, "error: unexpected argument: %s\n", argv[optind]);
        fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
        return -1;
    }

    return 0;
}
