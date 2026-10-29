#include "keyboard.h"

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC_EOI      0x20

#define KBD_DATA_PORT   0x60
#define KBD_STATUS_PORT 0x64
#define KEY_BUFFER_SIZE 128

static volatile char key_buffer[KEY_BUFFER_SIZE];
static volatile uint32_t key_head = 0;
static volatile uint32_t key_tail = 0;

static volatile bool shift_pressed = false;
static volatile bool caps_lock = false;
static volatile bool num_lock = true;

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port));
}

static const char normal_table[128] = {
    [0x02]='1',[0x03]='2',[0x04]='3',[0x05]='4',[0x06]='5',
    [0x07]='6',[0x08]='7',[0x09]='8',[0x0A]='9',[0x0B]='0',
    [0x0C]='-',[0x0D]='=',[0x0E]='\b',[0x0F]='\t',
    [0x10]='q',[0x11]='w',[0x12]='e',[0x13]='r',[0x14]='t',
    [0x15]='y',[0x16]='u',[0x17]='i',[0x18]='o',[0x19]='p',
    [0x1A]='[',[0x1B]=']',[0x1C]='\n',
    [0x1E]='a',[0x1F]='s',[0x20]='d',[0x21]='f',[0x22]='g',
    [0x23]='h',[0x24]='j',[0x25]='k',[0x26]='l',
    [0x27]=';',[0x28]='\'',[0x29]='`',[0x2B]='\\',
    [0x2C]='z',[0x2D]='x',[0x2E]='c',[0x2F]='v',[0x30]='b',
    [0x31]='n',[0x32]='m',[0x33]=',[',[0x34]='.',[0x35]='/',
    [0x39]=' '
};

static const char shifted_table[128] = {
    [0x02]='!',[0x03]='@',[0x04]='#',[0x05]='$',[0x06]='%',
    [0x07]='^',[0x08]='&',[0x09]='*',[0x0A]='(',[0x0B]=')',
    [0x0C]='_',[0x0D]='+',[0x0E]='\b',[0x0F]='\t',
    [0x10]='Q',[0x11]='W',[0x12]='E',[0x13]='R',[0x14]='T',
    [0x15]='Y',[0x16]='U',[0x17]='I',[0x18]='O',[0x19]='P',
    [0x1A]='{',[0x1B]='}',[0x1C]='\n',
    [0x1E]='A',[0x1F]='S',[0x20]='D',[0x21]='F',[0x22]='G',
    [0x23]='H',[0x24]='J',[0x25]='K',[0x26]='L',
    [0x27]=':',[0x28]='"',[0x29]='~',[0x2B]='|',
    [0x2C]='Z',[0x2D]='X',[0x2E]='C',[0x2F]='V',[0x30]='B',
    [0x31]='N',[0x32]='M',[0x33]='<',[0x34]='>',[0x35]='?',
    [0x39]=' '
};

static void push_key(char ch)
{
    if (!ch) return;

    uint32_t next = (key_head + 1) % KEY_BUFFER_SIZE;
    if (next != key_tail) {
        key_buffer[key_head] = ch;
        key_head = next;
    }
}

void keyboard_irq_handler(void)
{
    uint8_t status = inb(KBD_STATUS_PORT);

    if (status & 0x01) {
        uint8_t sc = inb(KBD_DATA_PORT);
        bool released = (sc & 0x80) != 0;
        uint8_t code = sc & 0x7F;

        if (code == 0x2A || code == 0x36) {
            shift_pressed = !released;
        } else if (code == 0x3A && !released) {
            caps_lock = !caps_lock;
        } else if (code == 0x45 && !released) {
            num_lock = !num_lock;
        } else if (!released && code < 128) {
            char ch = 0;

            if (code >= 0x47 && code <= 0x53) {
                if (num_lock) {
                    switch (code) {
                        case 0x47: ch = '7'; break;
                        case 0x48: ch = '8'; break;
                        case 0x49: ch = '9'; break;
                        case 0x4B: ch = '4'; break;
                        case 0x4C: ch = '5'; break;
                        case 0x4D: ch = '6'; break;
                        case 0x4F: ch = '1'; break;
                        case 0x50: ch = '2'; break;
                        case 0x51: ch = '3'; break;
                        case 0x52: ch = '0'; break;
                        case 0x53: ch = '.'; break;
                    }
                }
            } else {
                bool upper = shift_pressed ^ caps_lock;
                ch = upper ? shifted_table[code] : normal_table[code];
            }

            push_key(ch);
        }
    }

    outb(PIC1_COMMAND, PIC_EOI);
}

bool keyboard_getchar(char *out)
{
    if (!out || key_tail == key_head) return false;

    *out = key_buffer[key_tail];
    key_tail = (key_tail + 1) % KEY_BUFFER_SIZE;
    return true;
}

void keyboard_init(void)
{
    outb(PIC1_DATA, 0xFC);
    outb(0xA1, 0xFF);

    if (inb(KBD_STATUS_PORT) & 0x01)
        (void)inb(KBD_DATA_PORT);
}