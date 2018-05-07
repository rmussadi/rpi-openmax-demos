/*
 *
 */

#include "rpi-i420-framing.hpp"

void get_i420_frame_info(int width, int height, int buf_stride, int buf_slice_height, i420_frame_info *info)
{
    info->p_stride[0] = ROUND_UP_4(width);
    info->p_stride[1] = ROUND_UP_4(ROUND_UP_2(width) / 2);
    info->p_stride[2] = info->p_stride[1];
    info->p_offset[0] = 0;
    info->p_offset[1] = info->p_stride[0] * ROUND_UP_2(height);
    info->p_offset[2] = info->p_offset[1] + info->p_stride[1] * (ROUND_UP_2(height) / 2);
    info->size = info->p_offset[2] + info->p_stride[2] * (ROUND_UP_2(height) / 2);
    info->width = width;
    info->height = height;
    info->buf_stride = buf_stride;
    info->buf_slice_height = buf_slice_height;
    info->buf_extra_padding =
        buf_slice_height >= 0
        ? ((buf_slice_height && (height % buf_slice_height))
             ? (buf_slice_height - (height % buf_slice_height))
             : 0)
        : -1;
}

void dump_frame_info(const char *message, const i420_frame_info *info) {
    say("%s frame info:\n"
        "\tWidth:\t\t\t%d\n"
        "\tHeight:\t\t\t%d\n"
        "\tSize:\t\t\t%d\n"
        "\tBuffer stride:\t\t%d\n"
        "\tBuffer slice height:\t%d\n"
        "\tBuffer extra padding:\t%d\n"
        "\tPlane strides:\t\tY:%d U:%d V:%d\n"
        "\tPlane offsets:\t\tY:%d U:%d V:%d\n",
            message,
            info->width, info->height, info->size, info->buf_stride, info->buf_slice_height, info->buf_extra_padding,
            info->p_stride[0], info->p_stride[1], info->p_stride[2],
            info->p_offset[0], info->p_offset[1], info->p_offset[2]);
}
