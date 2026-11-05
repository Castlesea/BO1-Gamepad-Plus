# bo1pad

Melhorias de controle e janela sem bordas para **Call of Duty: Black Ops (2010)** no PC.

Este projeto **não inclui nenhum arquivo do jogo**. Você precisa ter a sua própria cópia original.

## O que faz

- **Controle com menos atraso:** desliga a aceleração de mira e a curva de resposta de console, e aplica zona morta circular nos analógicos.
- **D-pad nos menus:** faz o D-pad navegar como as setas do teclado, sem perder os slots de item na partida.
- **Janela sem bordas:** o `BlackOps Borderless.exe` abre o jogo na resolução do monitor, sem bordas.
- **Configuração ao vivo:** o `bo1pad.ini` é relido sozinho ao salvar, mesmo com o jogo aberto.

O controle é lido pelo próprio jogo. Nada aperta teclas nem cria um controle virtual no Windows. Existe um modo opcional (`AimMode=mouse`) que transforma o analógico direito em movimento de mouse dentro do jogo. Ele tira mais atraso da mira, mas deixa o aim assist de fora.

## Instalação

1. Baixe o `.zip` em [Releases](../../releases).
2. Extraia `xinput1_3.dll`, `bo1pad.ini` e `BlackOps Borderless.exe` na pasta onde está o `BlackOps.exe`.
3. Abra pelo `BlackOps Borderless.exe` e aceite o aviso do UAC. Ele pede administrador para poder abrir o jogo e alterar a janela.

O launcher muda `r_fullscreen`, `r_mode`, `vid_xpos` e `vid_ypos` em `players\config.cfg`. Se quiser, faça uma cópia desse arquivo antes.

**Desinstalar:** apague os três arquivos. Para voltar à tela cheia, mude nas opções de vídeo do jogo.

## Compatibilidade

Testado em **uma única instalação** com um controle XInput genérico. Outras versões do jogo não foram testadas.

Os ajustes dependem de endereços do `BlackOps.exe`. Se a sua versão for diferente, o `bo1pad.log` mostra `unknown BlackOps.exe build`. Nesse caso, os ajustes de dvars e do D-pad ficam desligados.

O jogo lê apenas o controle no slot 1 do XInput.

## Plutonium

**Não use a DLL no Plutonium:** ele tem anti-cheat, e os endereços dela são do `BlackOps.exe`.

Para desligar a aceleração de mira por lá, existe um script separado, que não faz nada em partidas marcadas como online. Copie `plutonium/bo1pad.gsc` para:

```
%LOCALAPPDATA%\Plutonium\storage\t5\scripts\sp\
```

Esse script ainda não foi testado em jogo. Para conferir, digite `aim_accel_turnrate_enabled` no console durante a partida: o valor deve ser `0`.

## Configuração

Principais opções do `bo1pad.ini`:

| Opção | O que faz |
|---|---|
| `AimMode` | `native` usa a mira do jogo; `mouse` usa o analógico direito como mouse |
| `DpadMenuArrows` | `1` faz o D-pad agir como as setas; `0` mantém o original |
| `Log` | `1` grava o `bo1pad.log` na pasta do jogo |
| `[LeftStick] Deadzone` | Zona morta do analógico esquerdo |
| `[MouseAim] Deadzone` | Zona morta do analógico direito, nos dois modos |
| `[MouseAim]` | Velocidade, curva e boost, usados no modo `mouse` |
| `[Dvars]` | Valores aplicados direto no jogo |

## Compilar

Precisa do Visual Studio com as ferramentas de C++. Coloque esta pasta dentro da pasta do jogo e rode:

```bat
build.bat
```

Os arquivos compilados são copiados para a pasta acima. O ícone é opcional: se existir um `blackops.ico` nesta pasta, ele entra no launcher. O ícone do jogo não faz parte do repositório.

## Aviso

Projeto de fãs, sem vínculo com a Activision ou a Treyarch. Call of Duty é marca da Activision.
