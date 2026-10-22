#include "idt.hpp"
#include "io.hpp"
#include "string.hpp"
#include "graphics.hpp"

#define IDT_ENTRIES 256

static idt_entry idt[IDT_ENTRIES];
static idt_ptr   idtp;
static isr_handler_t interrupt_handlers[IDT_ENTRIES];

// Extern ISR and IRQ assembly functions
extern "C" {
    void isr0();  void isr1();  void isr2();  void isr3();
    void isr4();  void isr5();  void isr6();  void isr7();
    void isr8();  void isr9();  void isr10(); void isr11();
    void isr12(); void isr13(); void isr14(); void isr15();
    void isr16(); void isr17(); void isr18(); void isr19();
    void isr20(); void isr21(); void isr22(); void isr23();
    void isr24(); void isr25(); void isr26(); void isr27();
    void isr28(); void isr29(); void isr30(); void isr31();

    void irq0();  void irq1();  void irq2();  void irq3();
    void irq4();  void irq5();  void irq6();  void irq7();
    void irq8();  void irq9();  void irq10(); void irq11();
    void irq12(); void irq13(); void irq14(); void irq15();

    void isr128(); // INT 0x80 (Syscalls)

    void idt_flush(uint32_t);
}

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low  = (base & 0xFFFF);
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].sel       = sel;
    idt[num].always0   = 0;
    idt[num].flags     = flags;
}

static void pic_remap() {
    // ICW1: start initialization sequence (in cascade mode)
    outb(0x20, 0x11);
    io_wait();
    outb(0xA0, 0x11);
    io_wait();

    // ICW2: Vector offset (Master: 32 = 0x20, Slave: 40 = 0x28)
    outb(0x21, 0x20);
    io_wait();
    outb(0xA1, 0x28);
    io_wait();

    // ICW3: Tell Master PIC there is a slave PIC at IRQ2 (0000 0100)
    outb(0x21, 0x04);
    io_wait();
    // ICW3: Tell Slave PIC its cascade identity (0000 0010)
    outb(0xA1, 0x02);
    io_wait();

    // ICW4: 8086/88 (MCS-80/85) mode
    outb(0x21, 0x01);
    io_wait();
    outb(0xA1, 0x01);
    io_wait();

    // Unmask IRQs: IRQ0 (Timer), IRQ1 (Keyboard), IRQ2 (Cascade), IRQ12 (Mouse)
    outb(0x21, 0xF8); // 1111 1000 -> enable IRQ0, IRQ1, IRQ2
    outb(0xA1, 0xEF); // 1110 1111 -> enable IRQ12 (PS/2 Mouse on slave)
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(0xA0, 0x20);
    }
    outb(0x20, 0x20);
}

void register_interrupt_handler(uint8_t n, isr_handler_t handler) {
    interrupt_handlers[n] = handler;
}

static const char* exception_messages[32] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Into Detected Overflow",
    "Out of Bounds",
    "Invalid Opcode",
    "No Coprocessor",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Bad TSS",
    "Segment Not Present",
    "Stack Fault",
    "General Protection Fault (#GP)",
    "Page Fault (#PF)",
    "Unknown Interrupt",
    "Coprocessor Fault",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Security Exception", "Reserved"
};

static void draw_hex32(int x, int y, uint32_t val, uint32_t color) {
    char buf[11];
    buf[0] = '0'; buf[1] = 'x';
    const char* hex = "0123456789ABCDEF";
    for (int i = 0; i < 8; i++) {
        buf[2 + i] = hex[(val >> ((7 - i) * 4)) & 0x0F];
    }
    buf[10] = '\0';
    draw_string(x, y, buf, color);
}

static void show_bsod(registers_t* regs) {
    fill_rect(0, 0, SCREEN_W, SCREEN_H, 0x003399); // Blue screen

    draw_string(40, 50, ":( FishOS has encountered a critical system error", 0xFFFFFF);
    draw_string(40, 80, "The operating system stopped execution to prevent hardware damage.", 0xCBD5E1);

    if (regs->int_no < 32) {
        draw_string(40, 130, "Exception:", 0xFBBF24);
        draw_string(140, 130, exception_messages[regs->int_no], 0xFFFFFF);
    }

    draw_string(40, 160, "Error Code:", 0x93C5FD);
    draw_hex32(140, 160, regs->err_code, 0xFFFFFF);

    draw_string(40, 200, "CPU Register Dump:", 0x38BDF8);

    draw_string(40, 230, "EIP:", 0xFFFFFF); draw_hex32(80, 230, regs->eip, 0xFFFFFF);
    draw_string(200, 230, "CS:", 0xFFFFFF);  draw_hex32(230, 230, regs->cs, 0xFFFFFF);
    draw_string(340, 230, "EFLAGS:", 0xFFFFFF); draw_hex32(410, 230, regs->eflags, 0xFFFFFF);

    draw_string(40, 260, "EAX:", 0xFFFFFF); draw_hex32(80, 260, regs->eax, 0xFFFFFF);
    draw_string(200, 260, "EBX:", 0xFFFFFF); draw_hex32(240, 260, regs->ebx, 0xFFFFFF);
    draw_string(360, 260, "ECX:", 0xFFFFFF); draw_hex32(400, 260, regs->ecx, 0xFFFFFF);
    draw_string(520, 260, "EDX:", 0xFFFFFF); draw_hex32(560, 260, regs->edx, 0xFFFFFF);

    draw_string(40, 290, "ESP:", 0xFFFFFF); draw_hex32(80, 290, regs->useresp, 0xFFFFFF);
    draw_string(200, 290, "EBP:", 0xFFFFFF); draw_hex32(240, 290, regs->ebp, 0xFFFFFF);
    draw_string(360, 290, "ESI:", 0xFFFFFF); draw_hex32(400, 290, regs->esi, 0xFFFFFF);
    draw_string(520, 290, "EDI:", 0xFFFFFF); draw_hex32(560, 290, regs->edi, 0xFFFFFF);

    if (regs->int_no == 14) {
        uint32_t cr2;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        draw_string(40, 330, "Page Fault Address (CR2):", 0xF87171);
        draw_hex32(280, 330, cr2, 0xF87171);
    }

    draw_string(40, 400, "System halted. Please reboot your computer.", 0xE2E8F0);

    gfx_present();
    while (true) {
        cli();
        __asm__ volatile ("hlt");
    }
}

extern "C" void isr_handler(registers_t* regs) {
    if (interrupt_handlers[regs->int_no] != nullptr) {
        interrupt_handlers[regs->int_no](regs);
    } else if (regs->int_no < 32) {
        show_bsod(regs);
    }
}

extern "C" void irq_handler(registers_t* regs) {
    if (interrupt_handlers[regs->int_no] != nullptr) {
        interrupt_handlers[regs->int_no](regs);
    }
    pic_send_eoi(regs->int_no - 32);
}

void idt_init() {
    idtp.limit = (sizeof(idt_entry) * IDT_ENTRIES) - 1;
    idtp.base  = reinterpret_cast<uint32_t>(&idt);

    memset(&idt, 0, sizeof(idt_entry) * IDT_ENTRIES);
    memset(interrupt_handlers, 0, sizeof(interrupt_handlers));

    pic_remap();

    // ISRs 0..31: 0x8E = 1000 1110 (Present, Ring 0, 32-bit Interrupt Gate)
    idt_set_gate(0,  reinterpret_cast<uint32_t>(isr0),  0x08, 0x8E);
    idt_set_gate(1,  reinterpret_cast<uint32_t>(isr1),  0x08, 0x8E);
    idt_set_gate(2,  reinterpret_cast<uint32_t>(isr2),  0x08, 0x8E);
    idt_set_gate(3,  reinterpret_cast<uint32_t>(isr3),  0x08, 0x8E);
    idt_set_gate(4,  reinterpret_cast<uint32_t>(isr4),  0x08, 0x8E);
    idt_set_gate(5,  reinterpret_cast<uint32_t>(isr5),  0x08, 0x8E);
    idt_set_gate(6,  reinterpret_cast<uint32_t>(isr6),  0x08, 0x8E);
    idt_set_gate(7,  reinterpret_cast<uint32_t>(isr7),  0x08, 0x8E);
    idt_set_gate(8,  reinterpret_cast<uint32_t>(isr8),  0x08, 0x8E);
    idt_set_gate(9,  reinterpret_cast<uint32_t>(isr9),  0x08, 0x8E);
    idt_set_gate(10, reinterpret_cast<uint32_t>(isr10), 0x08, 0x8E);
    idt_set_gate(11, reinterpret_cast<uint32_t>(isr11), 0x08, 0x8E);
    idt_set_gate(12, reinterpret_cast<uint32_t>(isr12), 0x08, 0x8E);
    idt_set_gate(13, reinterpret_cast<uint32_t>(isr13), 0x08, 0x8E);
    idt_set_gate(14, reinterpret_cast<uint32_t>(isr14), 0x08, 0x8E);
    idt_set_gate(15, reinterpret_cast<uint32_t>(isr15), 0x08, 0x8E);
    idt_set_gate(16, reinterpret_cast<uint32_t>(isr16), 0x08, 0x8E);
    idt_set_gate(17, reinterpret_cast<uint32_t>(isr17), 0x08, 0x8E);
    idt_set_gate(18, reinterpret_cast<uint32_t>(isr18), 0x08, 0x8E);
    idt_set_gate(19, reinterpret_cast<uint32_t>(isr19), 0x08, 0x8E);
    idt_set_gate(20, reinterpret_cast<uint32_t>(isr20), 0x08, 0x8E);
    idt_set_gate(21, reinterpret_cast<uint32_t>(isr21), 0x08, 0x8E);
    idt_set_gate(22, reinterpret_cast<uint32_t>(isr22), 0x08, 0x8E);
    idt_set_gate(23, reinterpret_cast<uint32_t>(isr23), 0x08, 0x8E);
    idt_set_gate(24, reinterpret_cast<uint32_t>(isr24), 0x08, 0x8E);
    idt_set_gate(25, reinterpret_cast<uint32_t>(isr25), 0x08, 0x8E);
    idt_set_gate(26, reinterpret_cast<uint32_t>(isr26), 0x08, 0x8E);
    idt_set_gate(27, reinterpret_cast<uint32_t>(isr27), 0x08, 0x8E);
    idt_set_gate(28, reinterpret_cast<uint32_t>(isr28), 0x08, 0x8E);
    idt_set_gate(29, reinterpret_cast<uint32_t>(isr29), 0x08, 0x8E);
    idt_set_gate(30, reinterpret_cast<uint32_t>(isr30), 0x08, 0x8E);
    idt_set_gate(31, reinterpret_cast<uint32_t>(isr31), 0x08, 0x8E);

    // IRQs 0..15 -> 32..47
    idt_set_gate(32, reinterpret_cast<uint32_t>(irq0),  0x08, 0x8E);
    idt_set_gate(33, reinterpret_cast<uint32_t>(irq1),  0x08, 0x8E);
    idt_set_gate(34, reinterpret_cast<uint32_t>(irq2),  0x08, 0x8E);
    idt_set_gate(35, reinterpret_cast<uint32_t>(irq3),  0x08, 0x8E);
    idt_set_gate(36, reinterpret_cast<uint32_t>(irq4),  0x08, 0x8E);
    idt_set_gate(37, reinterpret_cast<uint32_t>(irq5),  0x08, 0x8E);
    idt_set_gate(38, reinterpret_cast<uint32_t>(irq6),  0x08, 0x8E);
    idt_set_gate(39, reinterpret_cast<uint32_t>(irq7),  0x08, 0x8E);
    idt_set_gate(40, reinterpret_cast<uint32_t>(irq8),  0x08, 0x8E);
    idt_set_gate(41, reinterpret_cast<uint32_t>(irq9),  0x08, 0x8E);
    idt_set_gate(42, reinterpret_cast<uint32_t>(irq10), 0x08, 0x8E);
    idt_set_gate(43, reinterpret_cast<uint32_t>(irq11), 0x08, 0x8E);
    idt_set_gate(44, reinterpret_cast<uint32_t>(irq12), 0x08, 0x8E);
    idt_set_gate(45, reinterpret_cast<uint32_t>(irq13), 0x08, 0x8E);
    idt_set_gate(46, reinterpret_cast<uint32_t>(irq14), 0x08, 0x8E);
    idt_set_gate(47, reinterpret_cast<uint32_t>(irq15), 0x08, 0x8E);

    // INT 0x80 (128): Syscalls — 0xEE = 1110 1110 (Present, Ring 3 callable, 32-bit Interrupt Gate)
    idt_set_gate(128, reinterpret_cast<uint32_t>(isr128), 0x08, 0xEE);

    __asm__ volatile ("lidt %0" : : "m"(idtp));
}
