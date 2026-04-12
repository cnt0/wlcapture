#pragma once

#define _GNU_SOURCE

#include <getopt.h>
#include <string.h>

#include <libavcodec/codec.h>

enum cli_opts { OPT_CODEC, OPT_CODEC_OPTS, OPT_UID, OPT_HELP, OPT_LAST };

char HELP[] =
    "wlcapture [--codec=CODEC] [--codec-opts=key1=value1,...] [--uid=UID] "
    "[OUTPUT_FILE]\n\n"
    "Default CODEC is libjxl (JPEG XL).\n"
    "You can try any codec from `ffmpeg -encoders` command.\n\n"
    "Check the list of supported options for given codec here:\n"
    "https://ffmpeg.org/ffmpeg-codecs.html\n\n"
    "if --uid is given then the window with UID will be captured\n"
    "UID is foreign toplevel identifier, in sway terminology\n"
    "you can find it in `swaymsg -t get_tree -r`\n\n"
    "OUTPUT_FILE: where to save the encoded image. Possible values:\n"
    "(not given)\tprint binary data to stdout\n"
    "-\t\tprint binary data to stdout\n"
    "some path\tsave image to that path";

struct app_opts {
  const AVCodec *codec;
  AVDictionary *codec_opts;
  FILE *outfile;
  char uid[1024];
  size_t uid_len;
};

static int app_opts_parse(int argc, char *argv[], struct app_opts *opts) {
  static struct option cli_args[] = {
      {"codec", required_argument, NULL, OPT_CODEC},
      {"codec-opts", required_argument, NULL, OPT_CODEC_OPTS},
      {"uid", required_argument, NULL, OPT_UID},
      {"help", no_argument, NULL, OPT_HELP},
      {0, 0, 0, 0},
  };
  opts->outfile = stdout;

  int n_opts;
  int c;
  while ((c = getopt_long(argc, argv, "", cli_args, &n_opts)) != -1) {
    switch (c) {
      case OPT_CODEC:
        opts->codec = avcodec_find_encoder_by_name(optarg);
        if (!opts->codec) {
          printf("Codec '%s' not found", optarg);
          return EXIT_FAILURE;
        }
        break;
      case OPT_CODEC_OPTS:
        if (av_dict_parse_string(&opts->codec_opts, optarg, "=", ",", 0)) {
          printf("Cannot parse codec opts string '%s'\n", optarg);
        }
        break;
      case OPT_UID:
        opts->uid_len = strnlen(optarg, 1024);
        memcpy(opts->uid, optarg, opts->uid_len);
        break;
      case OPT_HELP:
        puts(HELP);
        return EXIT_SUCCESS;
      default:
        puts("Unknown option");
        return EXIT_FAILURE;
    }
  }
  if (optind < argc && strcmp(argv[optind], "-")) {
    opts->outfile = fopen(argv[optind], "wb");
  }
  if (!opts->outfile) {
    puts("Cannot open output file for writing");
    return EXIT_FAILURE;
  }

  // default value for codec
  if (!opts->codec) {
    opts->codec = avcodec_find_encoder_by_name("libjxl");
    if (!opts->codec) {
      puts("Codec 'libjxl' not found");
      return EXIT_FAILURE;
    }
  }
  return 0;
}

static void app_opts_free(struct app_opts *opts) {
  if (!opts) {
    return;
  }
  av_dict_free(&opts->codec_opts);
  fclose(opts->outfile);
}

