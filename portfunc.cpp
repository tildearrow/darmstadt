#include "darmstadt.h"

extern int dw;
extern int dh;

int set_hwframe_ctx(AVCodecContext *ctx, AVBufferRef *hw_device_ctx)
{
    AVBufferRef *hw_frames_ref;
    AVHWFramesContext *frames_ctx = NULL;
    int err = 0;
    printf("alloc try\n");
    if (!(hw_frames_ref = av_hwframe_ctx_alloc(hw_device_ctx))) {
        fprintf(stderr, "Failed to create VAAPI frame context.\n");
        return -1;
    }
    printf("success\n");
    frames_ctx = (AVHWFramesContext *)(hw_frames_ref->data);
    frames_ctx->format    = AV_PIX_FMT_VAAPI;
    frames_ctx->sw_format = AV_PIX_FMT_NV12;
    frames_ctx->width     = dw;
    frames_ctx->height    = dh;
    frames_ctx->initial_pool_size = 20;
    printf("ctx init\n");
    printf("what the\n");
    ctx->hw_frames_ctx = av_buffer_ref(hw_frames_ref);
    if (!ctx->hw_frames_ctx)
        err = AVERROR(ENOMEM);
    av_buffer_unref(&hw_frames_ref);
    return err;
}
static int encode_write(AVCodecContext *avctx, AVFrame *frame, FILE *fout)
{
    int ret = 0;
    AVPacket enc_pkt;
    av_init_packet(&enc_pkt);
    enc_pkt.data = NULL;
    enc_pkt.size = 0;
    if ((ret = avcodec_send_frame(avctx, frame)) < 0) {
        fprintf(stderr, "Error code: %d\n",ret);
        goto end;
    }
    while (1) {
        ret = avcodec_receive_packet(avctx, &enc_pkt);
        if (ret)
            break;
        enc_pkt.stream_index = 0;
        ret = fwrite(enc_pkt.data, enc_pkt.size, 1, fout);
        av_packet_unref(&enc_pkt);
    }
end:
    ret = ((ret == AVERROR(EAGAIN)) ? 0 : -1);
    return ret;
}
