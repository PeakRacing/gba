/* Actual native HLE IRQ admission, without changing guest timing. */
#if GBA_DYNAREC_PERSISTENT_HLE_IRQ && !defined(GBA_TEST_CANDIDATE_ONLY)
unsigned gba_test_hle_irq_entries;
static void hle_irq_admission_proof(void) {
    static const unsigned budgets[]={1,17,4096};
    for(unsigned thumb=0;thumb<2;thumb++)for(unsigned nesting=1;nesting<=2;nesting++) {
        setup(thumb,thumb?0x3001:0xe2800001,GBA_C|GBA_V);
        for(unsigned i=0;i<2;i++) {
            gba_t *g=&machine[i];gba_cpu_t *cpu=&g->machine.cpu;
            cpu->cpsr=(cpu->cpsr&~31u)|0x92u;cpu->hle_irq=nesting;cpu->wait_mask=1;
            if(thumb)gba_put16(g->memory.iwram+254,0xe7fe);
            else gba_put32(g->memory.iwram+252,0xeafffffe);
        }
        unsigned before=gba_test_hle_irq_entries;
        for(unsigned n=0;n<3;n++)run(0xfad10+thumb*16+nesting*4+n,budgets[n]);
        require(gba_test_hle_irq_entries>before,0xfad40+thumb*4+nesting);
        require(machine[1].machine.cpu.hle_irq==nesting && machine[1].machine.cpu.wait_mask==1,0xfad50+thumb*4+nesting);
    }
    /* Neither an inactive IntrWait nor the BIOS return trampoline is admitted.
     * The scalar path retains responsibility for clearing the HLE nesting. */
    for(unsigned bios=0;bios<2;bios++) {
        setup(0,0xe2800001,0);gba_t *g=&machine[1];gba_cpu_t *cpu=&g->machine.cpu;
        cpu->wait_mask=1;cpu->hle_irq=bios;cpu->cpsr=0x92;
        cpu->r[15]=bios?0x13c:0x03000000;cpu->pipeline_valid=cpu->fetch_sequential=1;
        cpu->pipeline[0]=bios?0xe25ef004:0xe2800001;cpu->pipeline[1]=0xe2800001;
        g->machine.last_fetch=cpu->r[15]+4;g->stepping=g->cpu_bulk=1;g->event_cycles=0x10000;
        gba_cpu_t saved=*cpu;
        require(!gba_persistent_run(g,128),0xfad60+bios);
        unsigned same=1;
        for(unsigned n=0;n<sizeof(saved);n++)same&=((const uint8_t *)&saved)[n]==((const uint8_t *)cpu)[n];
        require(same,0xfad64+bios);
        g->stepping=g->cpu_bulk=0;g->cpu_pending=g->event_cycles=0;
    }
    puts_uart("HLE IRQ native admission: ARM/Thumb nested handlers, budgets, waiting and BIOS boundary PASS\n");
}
#endif
