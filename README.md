# ESP32-C6 + Round Display: base independente

Projeto ESP-IDF para **XIAO ESP32-C6 + Round Display for XIAO**, com LCD GC9A01
240×240 e touch CHSC6X. Contém apenas a integração gráfica e os diagnósticos:
sem sensores externos, relógio, medição de bateria ou arquivos do iDroid.
O driver de toque foi copiado da versão corrigida do firmware integrado;
esta pasta é autossuficiente e pode ser usada fora do repositório TCC.

**Estado da validação:** compilação e testes de coordenadas podem ser executados
sem a placa. Cores, orientação, temporização e funcionamento elétrico ainda
precisam do ensaio físico descrito abaixo. A base preserva as configurações do
firmware do TCC; sua criação não constitui uma nova validação em hardware.

## Ambiente e dependências

Use **ESP-IDF 5.5.1** e **LVGL 8.3.11**. As bibliotecas diretas estão fixadas
em `main/idf_component.yml`; `dependencies.lock` registra a resolução com hashes.

As ferramentas Python também são fixadas: Component Manager **2.5.2** e
esp-idf-kconfig **2.5.4**, em `tests/requirements-build.txt`. Instale-as no
ambiente ESP-IDF antes de configurar. Um gerenciador antigo pode resolver
LVGL 9 e causar avisos de símbolos Kconfig desconhecidos, mesmo com o SDK
5.5.1; preserve a versão 8.3.11 e a troca de bytes RGB565 deste projeto.

No Windows, instale o SDK pelo [instalador oficial](https://dl.espressif.com/dl/esp-idf/)
e abra o atalho **ESP-IDF PowerShell** da versão 5.5.1 no menu Iniciar.
No VS Code com a extensão ESP-IDF configurada: `Ctrl+Shift+P` e
**ESP-IDF: Open ESP-IDF Terminal** ([guia oficial](https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/additionalfeatures/esp-terminal.html)).
Em um PowerShell comum, também é possível:

```powershell
# Ajuste o caminho conforme sua instalação.
. "$env:USERPROFILE\esp\v5.5.1\esp-idf\export.ps1"
idf.py --version
cd "C:\caminho\para\ESP32C6_RoundDisplay"
python -m pip install -r tests/requirements-build.txt
idf.py reconfigure
idf.py build
```

No Linux/macOS, carregue `. ~/esp/esp-idf/export.sh` e execute os mesmos comandos
`idf.py` na raiz deste projeto. O alvo `esp32c6` já está nos defaults.

`reconfigure` baixa automaticamente a LVGL 8.3.11 e as demais dependências para
`managed_components/`. A primeira resolução exige internet. Para adicionar
a versão exata a um projeto diferente, use `idf.py add-dependency "lvgl/lvgl==8.3.11"`.
Referência: [registro oficial da LVGL 8.3.11](https://components.espressif.com/components/lvgl/lvgl/versions/8.3.11?language=en).

Para gravar e acompanhar logs, substitua `COM5` pela porta da placa:

```sh
idf.py -p COM5 flash monitor
```

Saia do monitor com `Ctrl+]`. A gravação altera o firmware da placa;
os passos de compilação e testes nativos abaixo não gravam nada.

## Ligações preservadas do projeto original

| Sinal | GPIO ESP32-C6 |
|---|---:|
| LCD MOSI / SPI2 | 18 |
| LCD SCLK | 19 |
| LCD CS | 1 |
| LCD DC | 21 |
| Iluminação, D6 (ativo alto, conforme chave KE) | 16 |
| Touch SDA / I²C0 | 22 |
| Touch SCL | 23 |
| Touch INT | 17 |
| BOOT, ativo em nível baixo | 9 |

O touch usa endereço `0x2E`, I²C a 100 kHz. MISO e reset do LCD não são
controlados por este firmware. Alimentação, encaixe e jumpers devem corresponder
ao Round Display/XIAO utilizado. Confira o esquema antes de adaptar a outro ESP32-C6;
os números acima são GPIOs, não os nomes D0/D1 impressos na placa.

O console usa **USB Serial/JTAG**, sem UART0 nos GPIO16/17: esses pinos já são
usados pela iluminação e pela interrupção de toque. A iluminação é ligada em
nível alto no GPIO16, como na [biblioteca oficial da Seeed](https://github.com/Seeed-Studio/Seeed_Arduino_RoundDisplay/blob/main/src/lv_xiao_round_screen.h).

## Diagnóstico na tela

Pressione e solte **BOOT** para alternar as páginas. A navegação independe do toque.
Pressione BOOT depois de o firmware iniciar; mantê-lo pressionado durante reset
pode selecionar o modo de gravação.

1. **Colors:** seis blocos rotulados RED, GREEN, BLUE, WHITE, GRAY e BLACK.
   Confirme as cores visualmente, sobretudo vermelho/azul e branco/preto.
2. **Touch:** quatro alvos nos centros (60,70), (180,70), (60,170), (180,170).
   Toque e arraste: o marcador amarelo deve acompanhar o dedo; as coordenadas
   devem corresponder ao alvo e o marcador deve sumir ao soltar.
3. **Render:** animação com movimento baseado em tempo, taxa de refresh LVGL
   e tempo médio de renderização. Os logs a cada cinco segundos também mostram
   pixels renderizados, heap livre/mínimo e maior bloco disponível para DMA.

A taxa é de ciclos de refresh da LVGL, **não uma medição do FPS físico do painel**.
Uma tela estática deve produzir poucos ciclos. A média inclui a atividade do
intervalo anterior de cinco segundos, inclusive uma troca recente de página.
Os rótulos do diagnóstico usam caracteres ASCII para funcionar com a fonte padrão.

## Cores, orientação e desempenho

O caminho é LVGL → buffer RGB565 → porta LVGL → SPI/DMA → GC9A01.
Em `idf.py menuconfig → Round Display`, ajuste uma opção por vez e registre o resultado:

| Sintoma / objetivo | O que conferir |
|---|---|
| Vermelho e azul trocados | `Panel uses BGR element order` |
| Cores sem relação com o esperado | `LVGL → Color settings`: 16 bits e troca de bytes RGB565 (`CONFIG_LV_COLOR_16_SWAP=y`) |
| Branco/preto invertidos | `Invert panel colors` |
| Imagem girada/espelhada | Flags `LCD: swap/mirror` |
| Toque girado/espelhado | Flags `Touch: swap/mirror`; troca X/Y ocorre antes dos espelhamentos |
| Instabilidade em SPI | Reduzir clock de 40 para 20 MHz e comparar |
| Comparar throughput e RAM | Alterar linhas de buffer ou habilitar dois buffers DMA |

As opções BGR, troca de bytes e inversão corrigem coisas diferentes: evite mudar
todas juntas. A orientação inicial do LCD é swap XY + mirror X + mirror Y;
o touch usa swap XY + mirror X, aplicado apenas pelo driver CHSC6X.
O buffer padrão tem **14.400 pixels = 28.800 bytes** (60 linhas); com dois buffers,
57.600 bytes, além da memória da LVGL e das tarefas. Os 40 MHz de SPI representam
um teto teórico de aproximadamente 43 quadros inteiros/s para 240×240×16 bits,
antes de comandos e processamento; a taxa real deve ser medida na montagem.

O boot tenta liberar o barramento I²C antes de criar o driver, pois um reset USB
pode interromper uma leitura sem desligar o controlador de toque. Se SDA/SCL
continuarem presos ou a criação do input falhar, o log informa o erro, a página
de toque mostra `Touch unavailable` e o LCD continua operando com navegação pelo BOOT.
Não há reinicialização automática da aplicação por falha de setup do touch.
Os latches de saída são colocados em nível de liberação antes de habilitar o
modo open-drain, e o código aguarda a estabilização das linhas antes de medi-las.
Isso evita criar uma condição START durante a configuração dos GPIOs ou classificar
uma transição de subida como barramento preso. Em uma captura do hardware, o setup
passou a registrar `SDA=1 SCL=1` e `Touch enabled` após essa correção.

O driver lê cinco bytes de `0x2E` quando INT indica toque, seguindo o protocolo
da biblioteca Seeed. Não exige um probe de endereço para escrita durante o boot.
Registrar o input não confirma fisicamente o controlador: valide pressionando os
quatro alvos. Para isolar o LCD, desative `Enable CHSC6X touch` e recompile.

Se aparecer somente chuvisco, confira primeiro o monitor serial: um loop de
`ESP_ERROR_CHECK failed` / `abort()` antes da criação da interface deixa a memória
do painel sem a imagem esperada. Na revisão inicial, um probe I²C fatal causava
esse comportamento em uma montagem; esse caminho foi removido. Isso não comprova
que toda ocorrência de chuvisco tenha a mesma causa: se a aplicação estiver
estável, prossiga com a verificação de SPI, alimentação e painel.

Na montagem usada para diagnosticar essa falha, a revisão corrigida foi gravada
e os blocos de cores apareceram na tela. O log passou a publicar métricas sem o
ciclo de aborts observado anteriormente. O usuário confirmou também o toque e
arraste nos alvos, com marcador e coordenadas acompanhando o dedo após a correção
dos GPIOs. Permanecem os ensaios de fidelidade das cores, desempenho e duração
descritos no final deste guia; o resultado de uma montagem não valida todas as placas.

## Checagens sem gravar a placa

Teste nativo do código C usado pelo driver (necessita compilador C no computador):

```sh
python tests/run_host_tests.py --cc gcc
# Alternativa com Zig no PATH:
python tests/run_host_tests.py --cc "zig cc"
```

Verifica todas as oito combinações de orientação, cada ponto das resoluções
240×240 e 120×240, rejeição de coordenadas inválidas e a transformação da montagem.
Isso verifica a matemática; não simula o controlador I²C nem o painel.

Para compilar também os caminhos alternativos, use uma configuração independente:

```sh
idf.py -B build-alternative -D SDKCONFIG=sdkconfig.alternative -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;tests/sdkconfig.alternative.defaults" build
```

Essa configuração desliga o touch, ativa dois buffers, reduz SPI e altera as flags
de cor/orientação apenas para cobertura de compilação. **Não use esse perfil como
referência visual da montagem original.** Volte ao firmware padrão com `idf.py build`.

Na preparação desta base passaram o teste nativo, as compilações padrão e alternativa
no ESP-IDF 5.5.1 e a compilação de uma cópia limpa exportada pela ferramenta abaixo.
O binário da revisão inicial ocupou 466.432 bytes, deixando 56% da partição de aplicação livre.
A dependência `esp_lvgl_port` 2.9.0 ainda emite aviso sobre uma API de touch obsoleta;
o build conclui com `esp_lcd_touch` 1.2.1 fixado. Nenhum desses checks grava a placa.

## Criar uma cópia limpa para outro projeto

Na raiz desta pasta, escolha um destino novo, fora desta própria pasta:

```sh
python tools/copy_project.py ../MeuRoundDisplay --dry-run
python tools/copy_project.py ../MeuRoundDisplay
```

O primeiro comando lista os arquivos sem criar o destino. O segundo copia os
componentes, exemplo, documentação, testes, ferramentas, defaults e lock.
Não copia `build`, `managed_components`, `sdkconfig` local nem arquivos do TCC.
Recusa destinos existentes. Abra um terminal ESP-IDF na cópia e execute
`idf.py reconfigure` e `idf.py build`.

## Reutilizar os componentes

`components/round_display` concentra inicialização de LCD, LVGL e touch;
`components/chsc6x_touch` contém o driver adaptado. Para uma nova aplicação,
substitua `main/main.c` preservando manifesto e configuração gráfica. Inicialize
com `ESP_ERROR_CHECK(round_display_init(&display, &input))` e crie sua interface
sob `lvgl_port_lock()` / `lvgl_port_unlock()`. `input` é nulo com touch desabilitado.

A inicialização é única e possui falha fatal: reinicie após um erro parcial.
Não há API de desmontagem ou hot-plug. O componente possui SPI2 e I²C0; integrar
outros dispositivos nesses barramentos exige adaptar a propriedade dos handles
e a sincronização. Esta base não depende de arquivos de outra pasta do TCC.

## Roteiro mínimo no hardware

Registre modelo/revisão da placa, versão do firmware, fonte, clock SPI, linhas e
quantidade de buffers. Confira as seis cores, quatro alvos, arraste e soltura;
observe a animação e os logs por pelo menos dez minutos, procurando resets,
erros de barramento e queda contínua de heap. Repita após desligar/ligar a placa.
Compare buffer simples/duplo e 20/40 MHz sob as mesmas condições. Guarde os logs
e os resultados antes de usar a pasta como referência validada para outro produto.
