# Instalação

🇬🇧 [English](../../../INSTALLATION.md) · 🇫🇷 [Français](../fr/INSTALLATION.md) · 🇪🇸 [Español](../es/INSTALLATION.md) ·
🇨🇳 [中文](../zh-CN/INSTALLATION.md) · 🇮🇳 [हिन्दी](../hi/INSTALLATION.md) · 🇧🇷 Português · 🇷🇺 [Русский](../ru/INSTALLATION.md) ·
🇸🇦 [العربية](../ar/INSTALLATION.md) · 🇧🇩 [বাংলা](../bn/INSTALLATION.md) · 🇵🇰 [اردو](../ur/INSTALLATION.md)

Use somente o executável de instalação da [página de Releases do repositório oficial](https://github.com/ibrahimbouillet/AetherShell/releases).
A versão atual, o Alpha 4 [0.4.0-alpha.4](https://github.com/ibrahimbouillet/AetherShell/releases/tag/v0.4.0-alpha.4), é uma
prévia de instalação manual; a qualificação mais ampla da Versão 1 continua em aberto. A versão anterior
[0.2.0-alpha.2](https://github.com/ibrahimbouillet/AetherShell/releases/tag/v0.2.0-alpha.2) continua disponível.

Execute a instalação como o seu usuário comum do Windows. Desde o Alpha 4, a instalação fala o idioma que você escolheu
nas Configurações do Aether Shell ou, na falta dele, o idioma de exibição do seu Windows (qualquer um dos 41 idiomas da
interface), e o inglês caso contrário. Ao concluir, ela baixa da versão publicada os idiomas da interface que o seu
Windows tem (algumas dezenas de KB cada um); sem conexão, o Aether Shell os baixa ao iniciar. O assistente explica o
produto, exibe a licença MIT e permite
escolher uma pasta (padrão `%LOCALAPPDATA%\Programs\AetherShell`), atalhos e a inicialização opcional. Ele instala um
inicializador estável, uma instalação de manutenção e os arquivos de programa versionados. Pode criar um atalho no menu
Iniciar e uma entrada por usuário em Aplicativos instalados do Windows. Não usa direitos de administrador nem substitui o
Explorer. Em uma instalação interativa nova (desde o Alpha 2), **Iniciar ao entrar** vem visivelmente selecionado:
desmarque-o para recusar antes de instalar. Aceitar a instalação salva essa escolha; uma escolha já salva e uma
desativação feita no Gerenciador de Tarefas são respeitadas. A instalação silenciosa só ativa a inicialização com o
sinalizador explícito `--start-with-windows`; `--no-start-with-windows` seleciona desligado. Os perfis portáteis
continuam desligados até haver consentimento. O Windows controla a ordem de início ao entrar e pode atrasá-lo.

O shell V2 substitui visualmente a barra de tarefas do Windows por padrão quando o Aether Shell inicia. A instalação
divulga esse comportamento; o aplicativo mantém um auxiliar de recuperação e uma opção de recusa em **Configurações →
Área de trabalho**. Uma saída normal restaura a barra de tarefas do Windows. O Explorer continua em execução, e as
Configurações do Windows continuam configurando o Windows, e não o Aether Shell. As limitações de recuperação e os
comandos de emergência estão no [guia do usuário](USER_GUIDE.md).

Binários de aplicativo e de instalação sem assinatura podem acionar o SmartScreen. Esta versão alfa não tem certificado
de editor Authenticode. Confira a origem exata da versão e o SHA-256 antes de decidir executá-la:

```powershell
Get-FileHash .\AetherShell-0.4.0-alpha.4-win-x64-setup.exe -Algorithm SHA256
```

Compare com o arquivo de somas de verificação correspondente. Uma soma de verificação, por si só, não autentica um
editor. As atualizações automáticas exigem, além disso, o manifesto de versão assinado descrito em
[UPDATE_AND_ROLLBACK](../../../UPDATE_AND_ROLLBACK.md) (em inglês).

Use Aplicativos instalados → Aether Shell → Modificar para reparar a versão ativa com a instalação correspondente. A
desinstalação pede o encerramento normal do aplicativo e remove apenas as entradas e os arquivos registrados da
instalação. Manter os dados do usuário é o padrão; selecione explicitamente a remoção para apagar configurações, cache e
logs. Uma instalação mais antiga é rejeitada, a menos que o downgrade tenha sido permitido explicitamente. Guarde um
backup dos dados do usuário antes de qualquer downgrade intencional.

Os comandos silenciosos abaixo usam o executável de manutenção dentro da pasta de instalação; o nome do arquivo de
instalação baixado inclui a versão e a arquitetura. Use caminhos personalizados absolutos:

```powershell
.\AetherShellSetup.exe --silent --no-launch
.\AetherShellSetup.exe --silent --repair --no-launch
.\AetherShellSetup.exe --silent --uninstall --keep-user-data
```

Opções: `--install-root <pasta>`, `--data-dir <pasta>`, `--no-shortcuts`, `--no-launch`,
`--start-with-windows` (consentimento explícito), `--repair`, `--uninstall`, `--keep-user-data`,
`--no-start-with-windows` (recusa explícita),
`--remove-user-data`, `--allow-downgrade`, `--log <arquivo>`. O modo silencioso nunca faz perguntas. Códigos de saída:
0 sucesso, 1 falha da operação, 2 argumentos inválidos ou falha na configuração do caminho. A interface gráfica normal
impede o fechamento durante a operação em andamento; o cancelamento e a qualificação da autodesinstalação são
acompanhados em [KNOWN_LIMITATIONS](../../../KNOWN_LIMITATIONS.md) (em inglês).
