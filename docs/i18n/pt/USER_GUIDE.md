# Usar o Aether Shell

🇬🇧 [English](../../../USER_GUIDE.md) · 🇫🇷 [Français](../fr/USER_GUIDE.md) · 🇪🇸 [Español](../es/USER_GUIDE.md) ·
🇨🇳 [中文](../zh-CN/USER_GUIDE.md) · 🇮🇳 [हिन्दी](../hi/USER_GUIDE.md) · 🇧🇷 Português · 🇷🇺 [Русский](../ru/USER_GUIDE.md) ·
🇸🇦 [العربية](../ar/USER_GUIDE.md) · 🇧🇩 [বাংলা](../bn/USER_GUIDE.md) · 🇵🇰 [اردو](../ur/USER_GUIDE.md)

Inicie o Aether Shell pelo atalho do menu Iniciar ou pelo inicializador instalado. Ele abre duas superfícies da área de
trabalho: o Dock perto da parte inferior e a Barra de Status superior perto do topo. O Windows Explorer continua a
funcionar normalmente. Os itens do Dock têm menus de contexto. Selecione o logotipo da Barra superior para abrir o menu
dela, que também oferece Configurações e Sair.

Abra a Pesquisa pelo Dock ou com Ctrl+Alt+Espaço. Digite o nome de um aplicativo, use Cima/Baixo para selecionar e Enter
para abri-lo. Esc fecha a janela de pesquisa, assim como usar qualquer outra coisa, e toda abertura começa vazia. As
próprias ferramentas do Windows, como o Gerenciador de Dispositivos, também são encontradas. Só as entradas de
aplicativos descobertos podem ser iniciadas; o texto digitado nunca é um comando de shell. Ctrl+Alt+D dá o foco ao Dock.
Os dois atalhos são configuráveis em Configurações → Geral; os conflitos são informados ali.

O Dock distingue os aplicativos fixados dos outros aplicativos em execução. Use o menu de contexto dele ou
Configurações → Dock para fixar, desafixar e reordenar entradas e ajustar tamanho, ampliação e movimento. Um aplicativo
em execução pode ser ativado pelo seu item no Dock. A atribuição de algumas janelas UWP ainda precisa de qualificação.
Alpha 3: passar o mouse sobre um aplicativo em execução alarga o marcador dele e, após uma breve pausa, mostra um cartão
com pré-visualizações das suas janelas. Clique em uma pré-visualização para ir para aquela janela, ou no botão de fechar
dela para pedir que apenas aquela janela feche normalmente; é o aplicativo que decide se fecha (por exemplo, depois de
perguntar se você quer salvar).

A Barra superior exibe instantâneos obtidos de APIs documentadas do Windows. O estado desconhecido continua desconhecido.
Um PC de mesa sem bateria pode ocultar o item da bateria. Um indicador de microfone em uso não é compatível; as
informações de disponibilidade e silenciamento do dispositivo são separadas. Clique nos controles de áudio disponíveis
para alterar o volume de saída ou silenciar somente quando essa for a sua intenção. A Barra superior nunca mostra o nome
de uma rede. Desde a 3.3: nas Configurações rápidas, a seta do bloco de Wi-Fi lista as redes ao seu redor com o sinal de
cada uma. Escolha uma para entrar nela; o Aether pede a senha se for preciso, e o Windows a guarda. Nomes e senhas nunca
são registrados em log nem enviados. O Alpha 2 acrescenta a troca do idioma de entrada com Win+Espaço a partir do item
de teclado e as configurações de Clima a partir do item de clima visível. A ação agrupada de rede/áudio/bateria abre os
controles rápidos do Aether, incluindo o controle de mídia quando o Windows fornece uma sessão ativa, o volume, o brilho
compatível e blocos de ações do sistema. Os botões que o dispositivo não permite abrem os controles correspondentes do
Windows em vez de fingir alterar um dispositivo. A qualificação em dispositivos reais continua limitada. Clicar no
microfone silencia ou reativa o ponto de extremidade; o menu do clique direito oferece as configurações de privacidade e
**Iniciar a digitação por voz do Windows (Win+H)**. A ação de voz devolve o foco ao aplicativo anterior antes de pedir ao
Windows que abra o ditado no campo ativo dele. O Windows controla a disponibilidade do ditado, a permissão do microfone
e o processamento; o Aether Shell não captura nem armazena fala. Os testes automatizados não invocam o ditado real.

O Alpha 3 acrescenta três pontos de entrada na Barra superior. Clique na data e na hora para abrir a central de
notificações do Windows (Win+N no Windows 11, Win+A no Windows 10); o menu dela também abre o calendário. O Aether nunca
lê, apaga nem conta as suas notificações. A pequena seta abre diretamente abaixo dela os ícones ocultos da área de
notificação do próprio Windows; a seta aponta para cima enquanto eles estão abertos, e um segundo clique os fecha. Oculte
a seta em Configurações → Barra superior → **Mostrar o botão de ícones ocultos**. A ação agrupada de rede/áudio/bateria
abre o painel de Configurações rápidas do Aether, com uma segunda página e visualizações de detalhes para Wi-Fi,
Bluetooth, acessibilidade e saída de som; os botões que o Windows guarda para si abrem a página correspondente das
Configurações do Windows. No Windows 11, **Usar as Configurações rápidas do Windows (Windows 11)**, na mesma página, abre
o painel do próprio Windows.

**Idioma** (Alpha 4): o Aether Shell, instalação incluída, fala inglês, chinês (simplificado), hindi, espanhol, francês,
português, russo, árabe, bengali e urdu. Configurações → **Idioma** escolhe um; **Seguir o Windows**, o padrão, percorre
os idiomas de exibição do seu Windows na ordem em que você os classificou e usa o primeiro que for um dos dez, e o inglês
caso contrário. A mudança vale na hora para as barras, os menus, a Pesquisa e as Configurações, e a sua escolha fica
guardada nas suas configurações, que todas as versões leem: uma nova versão, um reparo ou uma reinstalação a encontram de
novo, e a instalação, o atualizador e o inicializador a usam. Com um idioma escolhido, a Barra superior escreve a data
nele; com Seguir o Windows, a data mantém o formato regional do seu Windows. Nomes de aplicativos, arquivos e redes nunca
são traduzidos. A Pesquisa mostra as ferramentas e as páginas de Configurações do Windows no idioma da interface e as
encontra pelo nome em qualquer um dos dez. O árabe e o urdu são lidos da direita para a esquerda.

As Configurações têm as páginas Dock, Barra superior, Clima, Atualizações, Geral, Sobre, Área de trabalho, Aparência,
Boas-vindas e Idioma. O Clima começa desativado. Escolha a localização automática do Windows com consentimento ou uma
localização manual antes de ativá-lo. O Windows pode negar a localização; nesse caso nenhuma localização nem clima é
inventado. A pesquisa manual de lugar envia o nome digitado ao Open-Meteo; as previsões enviam as coordenadas aproximadas
escolhidas. Ele não deduz a localização a partir do nome de uma rede Wi-Fi nem de um IP bruto. As previsões em cache
desaparecem após seis horas. Atribuição dos dados: Open-Meteo.com, CC BY 4.0.

**A janela do clima** (Alpha 4): clique no clima da Top Bar (o menu do clique direito mantém **Configurações do
clima…**) ou em Configurações → Clima → **Abrir o clima**. Ela mostra o lugar, sua hora local e o céu dessa hora, a
temperatura e a sensação térmica, as próximas 24 horas, 7 dias, o arco do sol, a umidade, o vento, o UV e a pressão, e
um mapa do país com suas temperaturas atuais. Pesquise outra cidade ou escolha **Minha localização**; se a localização
ainda não estiver permitida, a janela a pede ao Windows ou abre as configurações de localização dele. **Localização
precisa** (ativada por padrão) usa a posição do Windows com cerca de 100 m de precisão; desativada, ela é arredondada
para cerca de 10 km. O ícone do clima da Top Bar só se mexe sob o ponteiro. O mapa vem do Natural Earth (domínio
público) e fica armazenado no aplicativo. Com **Liquid** escolhido em Aparência, a janela do clima, as Configurações
rápidas, a Pesquisa, os menus e as prévias do Dock são de vidro como as barras, e as Configurações ganham um aspecto
Liquid; com **Grain**, todos são granulados.

Geral contém o consentimento de inicialização, as preferências de movimento e a edição dos atalhos. A instalação
interativa nova (desde o Alpha 2) seleciona visivelmente a inicialização ao entrar; desmarque essa opção antes da
instalação para recusá-la. As escolhas existentes são preservadas. A instalação silenciosa exige o sinalizador explícito
de inicialização. A inicialização é por usuário e opcional; Configurações → Geral → Aplicar pode alterá-la, e o
Gerenciador de Tarefas pode desativá-la de forma independente. O Windows controla a ordem e o atraso de início após a
entrada; ele pode mostrar a área de trabalho e a barra de tarefas nativas antes de o Aether estar pronto. O inicializador
estável instalado tenta de novo um número limitado de falhas anormais por exceção do Windows. Sair, atualizar ou
desinstalar de propósito e os erros comuns o interrompem; nenhum processo pode garantir que continue em execução diante de
todas as falhas. O alto contraste e o movimento reduzido seguem o Windows onde há suporte. A página Sobre contém a licença
MIT e a atribuição do projeto.

Na candidata V2, Área de trabalho contém **Substituir a barra de tarefas do Windows**, ativado por padrão (D23). Uma
recusa explícita já salva é preservada. Desmarque a opção e pressione Aplicar para restaurar a coexistência; marque-a e
pressione Aplicar para ativar a substituição de novo. Fechar a página ou navegar para outra descarta uma alteração não
aplicada. No modo de substituição, o Dock reserva espaço abaixo dos aplicativos maximizados. Janelas comuns posicionadas
manualmente ainda podem precisar ser movidas. A mensagem de estado mostra se a substituição está ativa ou indisponível.
As Configurações do Windows mantêm a página nativa da barra de tarefas; mude o comportamento do Aether Shell aqui. No
modo de substituição do Alpha 3, as janelas maximizadas elegíveis recebem cantos arredondados e uma borda fina, sem mudar
seu tamanho nem seu estado de maximização; o Aether desfaz isso ao sair. Alguns aplicativos e tipos de janela mantêm os
cantos quadrados. Desde a 3.3, apenas entre as barras. Com as duas barras ocultas, uma janela maximizada ocupa a tela
inteira, com cantos quadrados.

Desde o Alpha 4 (D72): toda janela maximizada entre as barras recebe cantos arredondados, seja qual for o aplicativo
(navegadores, Explorador de Arquivos, aplicativos empacotados e elevados), e mantém os próprios botões de legenda. O
Aether não toca mais na janela do aplicativo: pequenas peças dele, colocadas sobre os quatro cantos, mostram a área de
trabalho que está ali, ao vivo, com a borda fina em volta. Os cantos não têm suavização de serrilhado. Uma janela na
frente da maximizada os cobre como cobriria qualquer outra coisa.

**Barra superior como texto na área de trabalho** (Alpha 4, D71): Configurações → Barra superior → **Apresentação** →
**Texto na área de trabalho** remove a própria barra. Os mesmos itens são escritos no topo extremo da tela, diretamente
sobre a imagem da área de trabalho, em tinta clara ou escura escolhida a partir da imagem que está por baixo, com um halo
suave. Essa faixa fica livre de janelas; a margem superior e a opacidade do vidro não se aplicam. Com **Ocultar a Barra
superior automaticamente**, o texto volta sobre uma faixa de vidro enquanto uma janela estiver sob ele. O alto contraste
mantém a barra.

**Boas-vindas** (desde a 3.3): depois que você entra ou desbloqueia, o Aether Shell escreve uma breve saudação à mão,
preenchendo a tela, sobre a imagem desfocada da sua área de trabalho, e as barras aparecem quando ela termina: "Salut",
"Hello" ou "Hola", com o seu nome se você o informou. Ao desbloquear, ela já está lá, no lugar da área de trabalho. Toque,
clique ou pressione Esc para ignorá-la. A instalação pergunta o nome, de forma opcional (desde a 3.5); Configurações →
Boas-vindas a desativa e define o idioma e o nome. Se o arquivo da imagem foi movido ou apagado, as boas-vindas usam a
cópia que o Windows guarda dele. O nome e a sua imagem ficam neste PC; nenhum dos dois é enviado a lugar algum nem
registrado em log, e limpar o campo remove o nome. Ela só aparece depois que o Windows concluiu a sua entrada; a própria
tela de entrada do Windows não é alterada. Ao entrar, ela aparece assim que o Windows inicia o Aether, o que pode ser
depois da área de trabalho.

**Toque** (desde a 3.3): deslize o dedo para percorrer qualquer lista ou página longa; ela o acompanha diretamente,
continua deslizando quando você solta e volta suavemente depois de qualquer uma das extremidades. Um toque age quando você
levanta o dedo, um deslize nunca age, e manter um item pressionado na Barra superior ou no Dock abre o menu dele (segure
e mova para reordenar o Dock). Deslize para dentro a partir da borda superior ou inferior da tela para trazer de volta
uma barra oculta. Nas Configurações rápidas, deslize para cima ou para baixo para passar de uma página a outra. Em um
controle deslizante, mova para o lado para alterá-lo ou toque onde quiser colocá-lo; um deslize para cima ou para baixo
ali rola a página. Um controle deslizante das Configurações entra em vigor quando você solta.

A **Ocultação automática** (Alpha 3) é definida separadamente para cada barra: Configurações → Dock → **Ocultar o Dock
automaticamente** e Configurações → Barra superior → **Ocultar a Barra superior automaticamente**. As duas vêm
desativadas. Uma barra com ocultação automática desliza para fora enquanto uma janela de aplicativo a cobre. Deixe o
ponteiro parado na borda inferior da tela para trazer o Dock de volta, ou na borda superior extrema, sem margem, por um
quarto de segundo para trazer a Barra superior de volta; sair antes cancela, de modo que as guias e os botões de fechar
dos aplicativos ficam livres. Ela permanece enquanto você a usa ou enquanto um de seus menus ou painéis está aberto, e se
oculta de novo pouco depois. Na área de trabalho as barras voltam, mesmo com janelas atrás (Mostrar área de trabalho).
Uma barra com ocultação automática não reserva espaço na tela, então as janelas maximizadas preenchem a tela e a barra
revelada aparece sobre elas, como a barra de tarefas com ocultação automática do próprio Windows.

Área de trabalho também oferece Importar itens fixados da barra de tarefas do Windows. Ele lê os atalhos fixados
compatíveis disponíveis e adiciona as entradas que faltam, sem alterar os itens fixados do Windows nem substituir os seus
itens fixados do Aether. Alguns itens fixados do Windows 11 e a ordem original deles não estão disponíveis. O modo de
substituição importa os itens fixados compatíveis e fornece o Explorador de Arquivos uma vez. A sincronização automática
adiciona os novos itens fixados disponíveis na inicialização, ao ativar a sincronização ou ao reiniciar o Explorer;
preserva a ordem do usuário e as remoções explícitas. A importação manual pode restabelecer itens fixados removidos.
Aparência contém o botão da sincronização automática, o tema e o destaque, as predefinições de ícones e os controles de
movimento.

O Dock candidato tem as entradas Iniciar, Pesquisa e Visão de Tarefas. Arraste os ícones fixados para mudar a ordem
salva deles; arrastar um ícone que só está em execução muda a ordem desta sessão sem fixá-lo. Os metadados compatíveis de
ícone de perfil por janela podem distinguir os perfis do Chrome; quando faltam, o ícone continua genérico. Os menus de
contexto do Dock oferecem as ações de janela, nova instância e fixação aplicáveis, sem adivinhar um perfil.

Os menus da Barra superior expõem as configurações de relógio, energia, som e rede. A seta final visível da barra de
tarefas nativa foi removida no Alpha 2; desde o Alpha 3, a seta dos ícones ocultos dá acesso, em vez disso, ao excedente
da área de notificação do próprio Windows. A recuperação continua disponível pelas configurações de Área de trabalho, por
Sair e pelo comando de recuperação independente. O caminho interno de suspensão e retomada de compatibilidade continua
interno.

Para restaurar a barra nativa, desative a opção de Área de trabalho ou escolha Sair no menu da Barra superior.

A partir da pasta de instalação, `AetherShell.exe --restore-taskbar` pede a uma instância em execução que desative o
modo. Quando o Aether Shell não está em execução, o mesmo comando restaura a própria barra de tarefas do Windows:
- ele usa o estado salvo antes de a barra de tarefas ser ocultada;
- ele desativa o modo;
- ele mostra o resultado.

`AetherShell.exe --quit` pede a uma instância em execução que saia e não faz nada se não houver nenhuma.

Um auxiliar de recuperação separado restaura a barra de tarefas após uma falha do aplicativo, e o aplicativo faz o mesmo
se o auxiliar dele sair. **Reparar** ou **Desinstalar** em Configurações do Windows > Aplicativos > Aplicativos instalados
também restauram uma barra de tarefas que o Aether Shell deixou oculta. O reparo mantém as suas configurações, inclusive
a escolha de Área de trabalho, então o Aether Shell oculta a barra de tarefas de novo na próxima inicialização, a menos
que você desative a opção. Se nada disso funcionar, encerre os processos do Aether Shell no Gerenciador de Tarefas e
reinicie o Windows Explorer. A recuperação diante de todas as falhas e em todas as versões do Windows ainda não está
qualificada; veja as limitações conhecidas antes de ativar este modo.

Os dados locais ficam em `%LOCALAPPDATA%\AetherShell`: configurações, cache, atualizações e logs limitados. Abra a pasta
de logs em Geral. A exportação básica de diagnóstico contém a versão do produto e a compilação do Windows; revise você
mesmo quaisquer arquivos antes de compartilhá-los. Redefinir padrões pede confirmação e guarda um backup das
configurações. A desinstalação mantém os dados do usuário, a menos que a remoção seja selecionada explicitamente.

Consulte as [limitações conhecidas](../../../KNOWN_LIMITATIONS.md) (em inglês) para as lacunas de qualificação atuais.
