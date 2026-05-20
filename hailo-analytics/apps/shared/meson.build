shared_configs_install_dir = apps_install_dir + '/shared/resources/configs/'
# Install shared post-process JSON configs to /home/root/apps/shared/resources/configs/
shared_config_files = run_command('find', 'configs', '-type', 'f', check: true).stdout().strip().split('\n')
install_data(
  shared_config_files,
  install_dir: shared_configs_install_dir,
)
