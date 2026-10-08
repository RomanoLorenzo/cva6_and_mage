#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

// =============================================================================
// CVA6 DRAM
// =============================================================================
#define DRAM_BASE               ((volatile uint32_t *)(0x80000000UL))

// =============================================================================
// DEFINIZIONE INDIRIZZI BASE E REGISTRI DI CONTROLLO (MageCtrl @ 0x5000_0000)
// =============================================================================
#define MAGE_CTRL_BASE          0x50000000UL
#define MAGE_DATA_BASE          0xF0000000UL

#define BANK_SIZE_BYTES     0x1000U     // 1024 parole a 32 bit = 4096 byte
#define MAGE_BANK(n)        ((volatile uint32_t *)(MAGE_DATA_BASE + (n) * BANK_SIZE_BYTES))

/*******************************************************************************
 * DEFINIZIONE DEI REGISTRI DI CONTROLLO
 ******************************************************************************/
#define REG_STATUS          (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x00))
#define REG_GEN_CFG         (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x04))
#define REG_ILB_HWL         (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x08))
#define REG_BLOCK_SIZE      (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x0C))
#define REG_FLB_HWL         (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x10))
#define REG_INC_HWL         (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x14))

#define REG_STRIDES_0       (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x18))
#define REG_STRIDES_1       (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x1C))
#define REG_STRIDES_2       (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x20))
#define REG_STRIDES_3       (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x24))

#define REG_STRIDE_MODE_0   (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x28))
#define REG_STRIDE_MODE_1   (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x2C))
#define REG_STRIDE_MODE_2   (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x30))
#define REG_STRIDE_MODE_3   (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x34))

#define REG_ACC_HWLP_SEL_0  (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x38))

#define REG_CFG_PE_00       (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x3C))
#define REG_CFG_PE_01       (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x40))
#define REG_CFG_PE_10       (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x44))
#define REG_CFG_PE_11       (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x48))

#define REG_OUT_PEA_SEL     (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x4C))
#define REG_LOAD_AGE_SEL    (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x50))
#define REG_PEA_DMEM_SEL    (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x54))

#define REG_CFG_AGE_0       (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x58))
#define REG_CFG_AGE_1       (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x5C))
#define REG_CFG_AGE_2       (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x60))
#define REG_CFG_AGE_3       (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x64))

#define VECTOR_LEN          8           // Numero di campioni da trasferire

int main(void) {
    volatile uint32_t *dramptr = DRAM_BASE;
    uint32_t inerrors = 0;
    uint32_t outerrors = 0;

    /*==========================================================================
     * STEP 1: Creazione dei due vettori di campioni in memoria host
     *========================================================================*/
    uint32_t vec_in_0[VECTOR_LEN] = {0x00000000, 0x11111111, 0x22222222, 0x33333333,
                                     0x44444444, 0x55555555, 0x66666666, 0x77777777};
    uint32_t vec_in_1[VECTOR_LEN] = {0x88888888, 0x99999999, 0xaaaaaaaa, 0xbbbbbbbb,
                                     0xcccccccc, 0xdddddddd, 0xeeeeeeee, 0xffffffff};

    /*==========================================================================
     * STEP 2: Copia dei vettori nei due banchi di ingresso della SpM di Mage
     *========================================================================*/
    // Cosa sto facendo: Scrittura diretta via MMIO dei campioni nei banchi della SpM.
    // Perché lo sto facendo: Mage accede direttamente alla propria SpM multi-banco;
    // Banco 0 accoglierà vec_in_0 e Banco 1 accoglierà vec_in_1.
    volatile uint32_t *spm_bank0 = MAGE_BANK(0);
    volatile uint32_t *spm_bank1 = MAGE_BANK(1);
    for (int i = 0; i < VECTOR_LEN; i++) {
        spm_bank0[i] = vec_in_0[i];
        spm_bank1[i] = vec_in_1[i];
    }

    asm volatile("fence w, w" ::: "memory");

    // Eseguo controllo
    volatile uint32_t spm_bank0_readback[VECTOR_LEN];
    volatile uint32_t spm_bank1_readback[VECTOR_LEN];

    for (int i = 0; i < VECTOR_LEN; i++){
        spm_bank0_readback[i] = spm_bank0[i];
        spm_bank1_readback[i] = spm_bank1[i];

        if (spm_bank0_readback[i] != vec_in_0[i] || 
            spm_bank1_readback[i] != vec_in_1[i]){
            inerrors++;
        }
    }
    asm volatile("fence r, r" ::: "memory");

    for (int i = 0; i < VECTOR_LEN; i++) {
        DRAM_BASE[i] = spm_bank0_readback[i];
        DRAM_BASE[i + VECTOR_LEN] = spm_bank1_readback[i];
    }    
    asm volatile("fence w, w" ::: "memory");

    /*==========================================================================
     * STEP 3: Configurazione dei Registri di Controllo di Mage
     *========================================================================*/

    // --- 3.1 Configurazione Generale ---
    // Cosa sto facendo: Imposto II = 0 e modalità completamente statica.
    // Perché lo sto facendo: Nessun multiplexing temporale delle istruzioni; il pipeline
    // esegue un flusso di dati continuo ciclo per ciclo.
    REG_GEN_CFG = 0x00000000U;

    // --- 3.2 Hardware Loops (HWLP) ---
    // Cosa sto facendo: Inizializzo il limite inferiore di tutti i loop a 0.
    // Perché lo sto facendo: Il loop di caricamento/scrittura deve scorrere a partire dall'indice 0.
    REG_ILB_HWL = 0x00000000U;

    // Cosa sto facendo: Imposto il limite finale del Loop 0 (innermost) a (VECTOR_LEN - 1) = 7.
    // Perché lo sto facendo: I loop hardware di Mage valutano il ciclo in modo inclusivo (0..7),
    // eseguendo esattamente 8 iterazioni (un campione trasferito per ciclo).
    REG_FLB_HWL = (uint32_t)(VECTOR_LEN - 1);

    // Cosa sto facendo: Imposto l'incremento del Loop 0 a 1.
    // Perché lo sto facendo: L'indice deve avanzare di un elemento ad ogni ciclo di clock.
    REG_INC_HWL = 0x00000001U;

    // --- 3.3 Strides e Modalità di Indirizzamento AGEs ---
    // Cosa sto facendo: Imposto lo stride del Loop 0 a 1 parola (4 byte) per tutti e 4 gli AGE.
    // Perché lo sto facendo: Ad ogni iterazione, ciascun motore AGE deve puntare alla parola successiva
    // del rispettivo banco (accesso sequenziale).
    REG_STRIDES_0 = 0x00000001U; // AGE 0 (Stream 0, Load Banco 0)
    REG_STRIDES_1 = 0x00000001U; // AGE 1 (Stream 0, Load Banco 1)
    REG_STRIDES_2 = 0x00000001U; // AGE 2 (Stream 1, Store Banco 2)
    REG_STRIDES_3 = 0x00000001U; // AGE 3 (Stream 1, Store Banco 3)

    // Cosa sto facendo: Seleziono la modalità lineare per tutti gli AGE.
    // Perché lo sto facendo: L'indirizzamento è sequenziale piatto (Addr = Base + 1 * i), senza tiling.
    REG_STRIDE_MODE_0 = 0x00000000U;
    REG_STRIDE_MODE_1 = 0x00000000U;
    REG_STRIDE_MODE_2 = 0x00000000U;
    REG_STRIDE_MODE_3 = 0x00000000U;

    // --- 3.4 Istruzioni di Configurazione AGE ---
    // Layout dell'istruzione AGE:
    // [31]     isActive = 1
    // [30:23]  ageIvConstraint = 0
    // [22:15]  ivConst = 0 (offset base)
    // [14]     isAccStore = 0
    // [13]     loadNotStore (1 = LOAD, 0 = STORE)
    // [12:11]  blockSize = 0
    // [10:9]   startBank (indice del banco relativo al gruppo: 0 o 1)
    // [8:7]    numBanks = 0 (1 banco)
    // [6:4]    selIvConstraint = 4 (4 = UNCONSTRAINED, emette un indirizzo ad ogni iterazione)
    // [3:0]    hwlp = 0 (pilotato dal Loop 0)

    // AGE 0: LOAD da Banco 0 (Gruppo 0, Banco relativo 0)
    // Cosa sto facendo: Attivo AGE 0 in modalità LOAD non vincolata su Loop 0.
    // Perché lo sto facendo: Deve generare gli indirizzi di lettura per vec_in_0 dal Banco 0.
    REG_CFG_AGE_0 = (1U << 31) | (1U << 13) | (0U << 9) | (4U << 4) | (0U << 0); // 0x80002040

    // AGE 1: LOAD da Banco 1 (Gruppo 0, Banco relativo 1 -> bit 9 = 1)
    // Cosa sto facendo: Attivo AGE 1 in modalità LOAD su Banco 1.
    // Perché lo sto facendo: Deve generare gli indirizzi di lettura per vec_in_1 dal Banco 1.
    REG_CFG_AGE_1 = (1U << 31) | (1U << 13) | (1U << 9) | (4U << 4) | (0U << 0); // 0x80002240

    // AGE 2: STORE su Banco 2 (Gruppo 1, Banco relativo 0 -> loadNotStore = 0)
    // Cosa sto facendo: Attivo AGE 2 in modalità STORE per il primo banco di destinazione.
    // Perché lo sto facendo: Deve generare gli indirizzi di scrittura nel Banco 2 per il flusso in uscita.
    REG_CFG_AGE_2 = (1U << 31) | (0U << 13) | (0U << 9) | (4U << 4) | (3U << 0); // 0x80000040

    // AGE 3: STORE su Banco 3 (Gruppo 1, Banco relativo 1 -> startBank = 1, bit 9 = 1)
    // Cosa sto facendo: Attivo AGE 3 in modalità STORE per il secondo banco di destinazione.
    // Perché lo sto facendo: Deve generare gli indirizzi di scrittura nel Banco 3 per il secondo flusso.
    REG_CFG_AGE_3 = (1U << 31) | (0U << 13) | (1U << 9) | (4U << 4) | (3U << 0); // 0x80000240

    // --- 3.5 Crossbar Load Stream (Connessione Banchi -> PEA) ---
    // Cosa sto facendo: Assegno AGE 0 alla porta 0 (bits [1:0] = 0) e AGE 1 alla porta 1 (bits [3:2] = 1).
    // Perché lo sto facendo: La porta MEM_IN_0 del PEA riceverà i dati letti da Banco 0 tramite AGE 0,
    // e MEM_IN_1 riceverà i dati letti da Banco 1 tramite AGE 1.
    REG_LOAD_AGE_SEL = (1U << 2) | (0U << 0); // 0x00000004

    // --- 3.6 Configurazione dei Processing Elements (PEA) ---
    // Layout dell'istruzione PE: [12:8] opcode, [7:4] input B, [3:0] input A
    // Opcode FW_A = 11 (0x0B). Input MEM_IN_0 = 1, MEM_IN_1 = 2, CONSTANT = 0.

    // PE 0,0: Forwarding di MEM_IN_0
    // Cosa sto facendo: Imposto PE 0,0 su FW_A con ingresso A = MEM_IN_0.
    // Perché lo sto facendo: Inoltra i dati di vec_in_0 verso il suo registro di uscita senza alterarne il valore.
    REG_CFG_PE_00 = (0x0BU << 8) | (0U << 4) | (1U << 0); // 0x00000B01

    // PE 1,0: Forwarding di MEM_IN_1
    // Cosa sto facendo: Imposto PE 1,0 su FW_A con ingresso A = MEM_IN_1.
    // Perché lo sto facendo: Inoltra i dati di vec_in_1 verso il suo registro di uscita senza alterarne il valore.
    REG_CFG_PE_10 = (0x0BU << 8) | (0U << 4) | (2U << 0); // 0x00000B02

    // PE 0,1 e PE 1,1 non utilizzati
    REG_CFG_PE_01 = 0x00000000U; // NOP
    REG_CFG_PE_11 = 0x00000000U; // NOP

    // --- 3.7 Selezione Uscite Righe PEA ---
    // Layout: bits [1:0] = Riga 0 Out Sinistra, bits [3:2] = Riga 1 Out Sinistra,
    //         bits [5:4] = Riga 0 Out Destra,   bits [7:6] = Riga 1 Out Destra.
    // Cosa sto facendo: Seleziono la colonna 0 per l'uscita destra della riga 0 e colonna 0 per riga 1.
    // Perché lo sto facendo: Le uscite destre alimentano la crossbar dello Stream 1 (banchi 2 e 3).
    // Vogliamo che l'uscita destra della riga 0 prelevi PE 0,0 (colonna 0) e quella della riga 1 prelevi PE 1,0 (colonna 0).
    REG_OUT_PEA_SEL = (0U << 6) | (0U << 4) | (0U << 2) | (0U << 0); // 0x00000000

    // --- 3.8 Crossbar Store Stream (Connessione PEA -> Banchi di Uscita) ---
    // Cosa sto facendo: Assegno l'uscita di riga 0 (PE 0,0) al Banco 0 del Gruppo 1 (Banco 2, bits [5:4]=0),
    // e l'uscita di riga 1 (PE 1,0) al Banco 1 del Gruppo 1 (Banco 3, bits [7:6]=1 -> 1 << 6 = 0x40).
    // Perché lo sto facendo: Indirizza fisicamente i dati inoltrati da PE 0,0 nel Banco 2 e quelli da PE 1,0 nel Banco 3.
    REG_PEA_DMEM_SEL = (1U << 6) | (0U << 4); // 0x00000040

    asm volatile("fence w, w" ::: "memory");

    // --- 3.9 Avvio e Attesa Completamento ---
    // Cosa sto facendo: Scrivo 1 sul bit START del registro di stato.
    // Perché lo sto facendo: Fa scattare la FSM di Mage dallo stato IDLE a EXEC, attivando AGU e PEA.
    REG_STATUS = (1U << 0);

    // Cosa sto facendo: Eseguo un ciclo di polling sul bit DONE (bit 1) di REG_STATUS.
    // Perché lo sto facendo: Sincronizza il core CVA6 con il termine dell'elaborazione hardware prima di leggere i risultati.
    while (!((REG_STATUS >> 1) & 0x1U)) {
        // Attesa completamento
    }

    asm volatile("fence r, r" ::: "memory");

    /*==========================================================================
     * STEP 4: Lettura dai banchi di uscita e controllo di correttezza
     *========================================================================*/
    volatile uint32_t *spm_bank2 = MAGE_BANK(2);
    volatile uint32_t *spm_bank3 = MAGE_BANK(3);

    volatile uint32_t spm_bank2_readback[VECTOR_LEN];
    volatile uint32_t spm_bank3_readback[VECTOR_LEN];

    for (int i = 0; i < VECTOR_LEN; i++){
        spm_bank2_readback[i] = spm_bank2[i];
        spm_bank3_readback[i] = spm_bank3[i];

        if (spm_bank2_readback[i] != vec_in_0[i] || 
            spm_bank3_readback[i] != vec_in_1[i]){
            outerrors++;
        }
    }
    asm volatile("fence r, r" ::: "memory");

    for (int i = 0; i < VECTOR_LEN; i++) {
        DRAM_BASE[i + 2 * VECTOR_LEN] = spm_bank2_readback[i];
        DRAM_BASE[i + 3 * VECTOR_LEN] = spm_bank3_readback[i];
    }

    DRAM_BASE[4 * VECTOR_LEN] = inerrors;
    DRAM_BASE[4 * VECTOR_LEN + 1] = outerrors;

    asm volatile("fence w, w" ::: "memory");

    return 0;
}