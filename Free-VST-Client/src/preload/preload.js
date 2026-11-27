'use strict';

const { contextBridge, ipcRenderer } = require('electron');

const invoke = (ch, ...a) => ipcRenderer.invoke(ch, ...a);
const on = (ch) => (cb) => {
  const fn = (_e, payload) => cb(payload);
  ipcRenderer.on(ch, fn);
  return () => ipcRenderer.removeListener(ch, fn);
};

contextBridge.exposeInMainWorld('hub', {
  getState: () => invoke('state:get'),
  loadCatalog: () => invoke('catalog:load'),
  install: (id) => invoke('plugin:install', id),
  cancel: (jobId) => invoke('job:cancel', jobId),
  clearFinishedJobs: () => invoke('jobs:clearFinished'),
  uninstall: (id) => invoke('plugin:uninstall', id),
  openInstallDir: () => invoke('dir:open'),
  revealPlugin: (id) => invoke('plugin:reveal', id),
  chooseInstallDir: () => invoke('dir:choose'),
  resetInstallDir: () => invoke('dir:reset'),
  setCatalogUrl: (url) => invoke('settings:setCatalogUrl', url),
  setTheme: (theme) => invoke('settings:setTheme', theme),
  toggleWatch: (id) => invoke('watch:toggle', id),
  completeOnboarding: (opts) => invoke('onboarding:complete', opts),
  resetOnboarding: () => invoke('onboarding:reset'),
  setOnline: (on) => invoke('settings:setOnline', on),
  setStats: (on) => invoke('settings:setStats', on),
  setDesktopNotify: (on) => invoke('settings:setDesktopNotify', on),
  testNotification: () => invoke('notif:test'),
  checkOnline: () => invoke('online:check'),
  downloadLauncherUpdate: () => invoke('launcher:download'),
  restartToUpdate: () => invoke('launcher:restart'),
  installSelf: (opts) => invoke('app:installSelf', opts),
  createShortcuts: (opts) => invoke('app:shortcuts', opts),
  markNotificationsRead: () => invoke('notif:markRead'),
  clearNotifications: () => invoke('notif:clear'),
  win: {
    minimize: () => invoke('win:minimize'),
    toggleMaximize: () => invoke('win:toggleMax'),
    close: () => invoke('win:close'),
  },
  onJobs: on('jobs:update'),
  onState: on('state:changed'),
  onNotifications: on('notif:update'),
  onMaximized: on('win:maximized'),
  onVisibility: on('win:visible'),
  onCatalog: on('catalog:changed'),
  onLauncher: on('launcher:update'),
  onInstallProgress: on('app:installProgress'),
});
