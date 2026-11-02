# ☕ focOS — The Ambient E-Paper Desk OS

<p align="center">
  <img src="https://img.shields.io/badge/Status-In%20Development-blue?style=for-the-badge" alt="Status" />
  <img src="https://img.shields.io/badge/Platform-ESP--IDF%20v5.x-orange?style=for-the-badge" alt="ESP-IDF" />
  <img src="https://img.shields.io/badge/Target-ESP32%20%7C%20ESP32--S3-green?style=for-the-badge" alt="Hardware" />
  <img src="https://img.shields.io/badge/Display-WeAct%203.7%22%20E--Paper-black?style=for-the-badge" alt="E-Paper" />
  <img src="https://img.shields.io/badge/License-Apache--2.0-yellow?style=for-the-badge" alt="License" />
</p>

> **focOS** é um sistema operacional minimalista e de baixo atrito cognitivo (*Calm Technology*) projetado para displays físicos de tinta eletrônica (E-Paper) em mesas de trabalho. Criado para combater a desorganização crônica, a sobrecarga de estímulos e a procrastinação executiva.

---

## 💡 A Filosofia do focOS

Dispositivos e aplicativos tradicionais falham com frequência para quem luta contra a falta de rotina porque competem pela sua atenção: notificações intrusivas, luz azul e excesso de opções geram ansiedade e paralisia.

O **focOS** resgata a calma do papel impresso na sua mesa:
* **Sempre Visível (0% Luz Emitida):** Sem tela apagada no bolso. A prioridade está sempre no seu campo de visão natural.
* **Micro-Vitórias Táteis:** Marque tarefas concluídas com um clique prazeroso em um botão físico (Rotary Knob), gerando estímulo positivo de dopamina.
* **Arquitetura Modular (OS):** Construído sobre o **ESP-IDF (FreeRTOS)** com ciclo de vida de apps desacoplado, permitindo que a comunidade crie novos módulos (hábitos, timers, pomodoro, previsão do tempo e muito mais).

---

## 🖥️ Especificações de Hardware Recomendadas

* **Microcontrolador:** ESP32 (WROOM-32 30 pinos) ou ESP32-S3
* **Display:** WeAct Studio 3.7" E-Paper (416x240 pixels, controlador UC8253C com suporte a *Fast Partial Refresh ~0.3s*)
* **Input Físico:** Rotary Encoder KY-040 com botão integrado (leitura via periférico de hardware `PCNT`)
* **Backend:** Supabase (PostgreSQL + Realtime WebSockets)
* **App Companheiro:** Flutter (Mobile, Desktop macOS/Windows/Linux e Web PWA)

---

## 🗺️ Roadmap de Versões

* **v1.0 (MVP de Mesa):** Firmware ESP-IDF nativo + Driver UC8253C + Rotary Encoder + Lista do Dia & Modo Hiperfoco + Sincronização Supabase.
* **v2.0 (Ecossistema Conectado):** Onboarding sem código via QR Code na tela E-Paper + Bluetooth Low Energy (BLE) + Rastreador de Hábitos (Streaks) + App Flutter completo.
* **v3.0 (Central com IA & Sensores):** Quebra automática de tarefas com IA (Gemini API) + Brain Dump por voz + Sensor de presença mmWave na mesa + Qualidade do ar ($\text{CO}_2$).
* **v4.0+ (Dynamic App Engine):** Runtime WebAssembly (WASM) / Lua para carregamento dinâmico de apps criados pela comunidade sem recompilar o firmware.

---

## 📚 Documentação do Projeto

* 📋 **[Blueprint Arquitetural Completo](PROJETO_CENTRAL_ORGANIZACAO.md)**: Análise técnica detalhada, circuitos de proteção do display, pinouts e fluxos de dados.
* 🗂️ **[Backlog & User Stories](BACKLOG_USER_STORIES.md)**: Todas as histórias de usuário detalhadas com critérios de aceite prontas para o GitHub Projects.

---

## 📄 Licença

Distribuído sob a licença **Apache 2.0** (ou MIT). Aberto para toda a comunidade Maker e entusiastas de produtividade.

