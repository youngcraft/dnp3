#include <dnp3hammer.h>

#include <hammer/hammer.h>
#include <hammer/glue.h>
#include "hammer.h"
#include <string.h>
#include "util.h"


HParser *dnp3_p_link_frame;


// from IEEE Std 1815-2012 Annex E
static const uint16_t crctable[256] = {
    0x0000, 0x365E, 0x6CBC, 0x5AE2, 0xD978, 0xEF26, 0xB5C4, 0x839A,
    0xFF89, 0xC9D7, 0x9335, 0xA56B, 0x26F1, 0x10AF, 0x4A4D, 0x7C13,
    0xB26B, 0x8435, 0xDED7, 0xE889, 0x6B13, 0x5D4D, 0x07AF, 0x31F1,
    0x4DE2, 0x7BBC, 0x215E, 0x1700, 0x949A, 0xA2C4, 0xF826, 0xCE78,
    0x29AF, 0x1FF1, 0x4513, 0x734D, 0xF0D7, 0xC689, 0x9C6B, 0xAA35,
    0xD626, 0xE078, 0xBA9A, 0x8CC4, 0x0F5E, 0x3900, 0x63E2, 0x55BC,
    0x9BC4, 0xAD9A, 0xF778, 0xC126, 0x42BC, 0x74E2, 0x2E00, 0x185E,
    0x644D, 0x5213, 0x08F1, 0x3EAF, 0xBD35, 0x8B6B, 0xD189, 0xE7D7,
    0x535E, 0x6500, 0x3FE2, 0x09BC, 0x8A26, 0xBC78, 0xE69A, 0xD0C4,
    0xACD7, 0x9A89, 0xC06B, 0xF635, 0x75AF, 0x43F1, 0x1913, 0x2F4D,
    0xE135, 0xD76B, 0x8D89, 0xBBD7, 0x384D, 0x0E13, 0x54F1, 0x62AF,
    0x1EBC, 0x28E2, 0x7200, 0x445E, 0xC7C4, 0xF19A, 0xAB78, 0x9D26,
    0x7AF1, 0x4CAF, 0x164D, 0x2013, 0xA389, 0x95D7, 0xCF35, 0xF96B,
    0x8578, 0xB326, 0xE9C4, 0xDF9A, 0x5C00, 0x6A5E, 0x30BC, 0x06E2,
    0xC89A, 0xFEC4, 0xA426, 0x9278, 0x11E2, 0x27BC, 0x7D5E, 0x4B00,
    0x3713, 0x014D, 0x5BAF, 0x6DF1, 0xEE6B, 0xD835, 0x82D7, 0xB489,
    0xA6BC, 0x90E2, 0xCA00, 0xFC5E, 0x7FC4, 0x499A, 0x1378, 0x2526,
    0x5935, 0x6F6B, 0x3589, 0x03D7, 0x804D, 0xB613, 0xECF1, 0xDAAF,
    0x14D7, 0x2289, 0x786B, 0x4E35, 0xCDAF, 0xFBF1, 0xA113, 0x974D,
    0xEB5E, 0xDD00, 0x87E2, 0xB1BC, 0x3226, 0x0478, 0x5E9A, 0x68C4,
    0x8F13, 0xB94D, 0xE3AF, 0xD5F1, 0x566B, 0x6035, 0x3AD7, 0x0C89,
    0x709A, 0x46C4, 0x1C26, 0x2A78, 0xA9E2, 0x9FBC, 0xC55E, 0xF300,
    0x3D78, 0x0B26, 0x51C4, 0x679A, 0xE400, 0xD25E, 0x88BC, 0xBEE2,
    0xC2F1, 0xF4AF, 0xAE4D, 0x9813, 0x1B89, 0x2DD7, 0x7735, 0x416B,
    0xF5E2, 0xC3BC, 0x995E, 0xAF00, 0x2C9A, 0x1AC4, 0x4026, 0x7678,
    0x0A6B, 0x3C35, 0x66D7, 0x5089, 0xD313, 0xE54D, 0xBFAF, 0x89F1,
    0x4789, 0x71D7, 0x2B35, 0x1D6B, 0x9EF1, 0xA8AF, 0xF24D, 0xC413,
    0xB800, 0x8E5E, 0xD4BC, 0xE2E2, 0x6178, 0x5726, 0x0DC4, 0x3B9A,
    0xDC4D, 0xEA13, 0xB0F1, 0x86AF, 0x0535, 0x336B, 0x6989, 0x5FD7,
    0x23C4, 0x159A, 0x4F78, 0x7926, 0xFABC, 0xCCE2, 0x9600, 0xA05E,
    0x6E26, 0x5878, 0x029A, 0x34C4, 0xB75E, 0x8100, 0xDBE2, 0xEDBC,
    0x91AF, 0xA7F1, 0xFD13, 0xCB4D, 0x48D7, 0x7E89, 0x246B, 0x1235
};

uint16_t dnp3_crc(uint8_t *bytes, size_t len)
{
    uint16_t crc = 0;
    for(size_t i=0; i<len; i++) {
        crc = (crc>>8) ^ crctable[((uint8_t)crc ^ bytes[i]) & 0xFF];
    }
    crc = ~crc; // invert

    return crc;
}

static bool validate_crc(HParseResult *p, void *user)
{
    uint8_t buf[16];

    // p = ((byte...) crc)
    HCountedArray *bytes = H_FIELD_SEQ(0);
    uint16_t crc = H_FIELD_UINT(1);

    assert(bytes->used <= 16);
    for(size_t i=0; i<bytes->used; i++) {
        buf[i] = H_CAST_UINT(bytes->elements[i]);
    }

    uint16_t compcrc = dnp3_crc(buf, bytes->used);
#ifdef PRINTCRC
    if(crc != compcrc)
        fprintf(stderr, "crc %.2X%.2X\n", compcrc%256, compcrc>>8);
#endif
    return (crc == compcrc);
}

#define act_valid_crc h_act_first

static HParser *bytes_crc_(size_t n)
{
    H_RULE(byte,    h_uint8());
    H_RULE(crc,     h_uint16());

    H_RULE (bytes_crc, h_sequence(h_repeat_n(byte, n), crc, NULL));
    H_ARULE(valid_crc, h_attr_bool(bytes_crc, validate_crc, NULL));

    return h_choice(valid_crc, h_ignore(bytes_crc), NULL);
}

// helper to copy bytes from an HSequence into an array
static size_t bytecpy(uint8_t *out, const HCountedArray *a)
{
    for(size_t i=0; i<a->used; i++)
        *out++ = H_CAST_UINT(a->elements[i]);
    return a->used;
}

static HParsedToken *act_header(const HParseResult *p, void *user)
{
    DNP3_Frame *frame = H_ALLOC(DNP3_Frame);

    // p = (start,len,(func,fcv/dfc,fcb,prm,dir),dest,source)
    frame->len = H_FIELD_UINT(1) - 5;   // payload length, excl. header bytes

    frame->dir = H_FIELD_UINT(2,4);
    bool prm = H_FIELD_UINT(2,3);
    frame->fcb = H_FIELD_UINT(2,2);
    if(prm)
        frame->fcv = H_FIELD_UINT(2,1);
    else
        frame->dfc = H_FIELD_UINT(2,1);
    frame->func = (prm << 4) | H_FIELD_UINT(2,0);

    frame->destination = H_FIELD_UINT(3);
    frame->source = H_FIELD_UINT(4);

    return H_MAKE(DNP3_Frame, frame);
}

static HParsedToken *attach_payload(const HParseResult *p, void *user)
{
    DNP3_Frame *hdr = user;
    DNP3_Frame *frame = H_ALLOC(DNP3_Frame);

    *frame = *hdr;
    if(p->ast) {
        HBytes b = H_CAST_BYTES(p->ast);
        assert(b.len == hdr->len);
        frame->payload = (uint8_t *)b.token;
    } else {
        frame->payload = NULL;
    }

    return H_MAKE(DNP3_Frame, frame);
}

// prebaked parsers for every size of payload
static HParser *payload[256] = {NULL};

static HParser *k_frame(HAllocator *mm__, const HParsedToken *p, void *user)
{
    DNP3_Frame *hdr = H_CAST(DNP3_Frame, p);
    size_t n;

    if(hdr->len > 0) {
        assert(hdr->len <= 255);
        n = hdr->len;
    } else {
        n = 0;
    }

    return h_action__m(mm__, payload[n], attach_payload, hdr);
}

static bool validate_blocks(HParseResult *p, void *user)
{
    HCountedArray *blocks = H_FIELD_SEQ(0);
    int q = (intptr_t)user;

    // last block present (=valid)?
    if(H_CAST_SEQ(p->ast)->used < 2)
        return false;

    // all 16-byte blocks present (=valid)?
    return (blocks->used == q);
}

static HParsedToken *act_assemble(const HParseResult *p, void *user)
{
    // p = ((block...), block)
    HCountedArray *blocks = H_FIELD_SEQ(0);
    HCountedArray *last = H_FIELD_SEQ(1);

    size_t len = blocks->used * 16 + last->used;
    if(len == 0)
        return NULL;

    // assemble data
    uint8_t *buf = h_arena_malloc(p->arena, len);
    uint8_t *out = buf;
    for(size_t i=0; i<blocks->used; i++) {
        HCountedArray *a = H_CAST_SEQ(blocks->elements[i]);
        assert(a->used == 16);
        out += bytecpy(out, a);
    }
    bytecpy(out, last);

    return H_MAKE_BYTES(buf, len);
}

static bool not_null(HParseResult *p, void *user)
{
    return (p->ast != NULL);
}

void dnp3_p_init_link(void)
{
    // bake basic parsers for CRC-ed data blocks
    HParser *bytes_crc[17];          // yield bytes if CRC valid, NULL otherwise
    bytes_crc[0] = h_sequence(NULL); // empty sequence - no crc
    for(int i=1; i<=16; i++)
        bytes_crc[i] = bytes_crc_(i);

    H_RULE(bit,     h_bits(1, false));
    H_RULE(address, h_uint16());

    H_RULE(start,   h_token("\x05\x64", 2));
    H_RULE(len,     h_uint8());
    H_RULE(func,    h_bits(4, false));
                              /* --- fcv fcb prm dir --- */
                              /*     dfc                 */
    H_RULE(ctrl,    h_sequence(func, bit,bit,bit,bit, NULL));
    H_RULE(dest,    address);
    H_RULE(source,  address);
    H_RULE(crc,     h_uint16());

    H_RULE(header_, h_sequence(start, len, ctrl, dest, source, NULL));
    H_RULE(hdr_crc, h_attr_bool(bytes_crc[8], not_null, NULL));
    H_ARULE(header, h_middle(h_and(hdr_crc), header_, crc));
                              
    H_RULE(frame,   h_bind(header, k_frame, NULL));

    dnp3_p_link_frame = little_endian(frame);

    // bake parsers for data payloads so they don't have to be created on every
    // call to k_frame...
    for(int q=0; q<16; q++) {
        H_RULE(fullblocks, h_repeat_n(bytes_crc[16], q));

        for(int r=0; r<16; r++) {
            int n = q*16 + r;
            intptr_t qp = q;

            H_RULE (lastblock, bytes_crc[r]);
            H_RULE (blocks,    h_sequence(fullblocks, lastblock, NULL));
            H_ARULE(assemble,  h_attr_bool(blocks, validate_blocks, (void*)qp));
            H_RULE (skip,      h_ignore(blocks));

            payload[n] = h_choice(assemble, skip, NULL);
        }
    }
}

bool dnp3_link_validate_frame(const DNP3_Frame *frame)
{
    #define REQUIRE(exp) do {if(!(exp)) return false;} while(0)

    // must have payload for USER_DATA and for USER_DATA only
    if(frame->func == DNP3_CONFIRMED_USER_DATA ||
       frame->func == DNP3_UNCONFIRMED_USER_DATA) {
        REQUIRE(frame->len > 0);
        REQUIRE(frame->payload != NULL);
    } else {
        REQUIRE(frame->len == 0);
        REQUIRE(frame->payload == NULL);
    }

    // valid function codes
    switch(frame->func) {
    case DNP3_RESET_LINK_STATES:
    case DNP3_TEST_LINK_STATES:
    case DNP3_CONFIRMED_USER_DATA:
    case DNP3_UNCONFIRMED_USER_DATA:
    case DNP3_REQUEST_LINK_STATUS:
    case DNP3_ACK:
    case DNP3_NACK:
    case DNP3_LINK_STATUS:
    case DNP3_NOT_SUPPORTED:
        break;
    default:
        return false;
    }

    if(dnp3_link_prm(frame)) {
        // FCV is completely determined by function code
        REQUIRE(frame->fcv == dnp3_link_fcv(frame));
    }

    // XXX correct to ignore FCB on secondary frames?!

    // valid source address range: 0 - 0xFFEF(65519)
    REQUIRE(frame->source <= 0xFFEF);

    // reserved destination addresses: 0xFFF0-0xFFFB (cf. AN2013-004b)
    REQUIRE(frame->destination < 0xFFF0 || frame->destination > 0xFFFB);

    // broadcast addresses (0xFFFD-0xFFFF) only valid for USER_DATA
    if(frame->destination >= 0xFFFD) {
        REQUIRE(frame->func == DNP3_CONFIRMED_USER_DATA ||
                frame->func == DNP3_UNCONFIRMED_USER_DATA);
    }

    #undef REQUIRE
    return true;
}

bool dnp3_link_fcv(const DNP3_Frame *frame)
{
    assert(dnp3_link_prm(frame));
    switch(frame->func) {
    case DNP3_TEST_LINK_STATES:
    case DNP3_CONFIRMED_USER_DATA:
        return 1;
    default:
        return 0;
    }
}
