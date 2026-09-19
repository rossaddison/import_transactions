<?php
define ('SS_IMPORTTRANSACTIONS', 101<<8);

final class hooks_import_transactions extends hooks {
	/** @var string FrontAccounting's hooks class declares this property untyped, so it must stay untyped here */
	public $module_name = 'import_transations';

	/*
		Install additonal menu options provided by module
	*/
	#[\Override]
	public function install_options($app): void {
		/** @var string $path_to_root */
		global $path_to_root;

		switch($app->id) {
			case 'GL':
				$app->add_rapp_function(2, _('Import &Transactions'),
					$path_to_root.'/modules/import_transactions/import_transactions.php', 'SA_CSVTRANSACTIONS');
		}
	}

	/**
	 * @psalm-pure
	 * @return array{0: array<string, array{0: int, 1: string}>, 1: array<int, string>}
	 */
	#[\Override]
	public function install_access()
	{
		$security_sections = array();
		$security_areas = array();

		$security_sections[SS_IMPORTTRANSACTIONS] =	_("Import Transactions");

		$security_areas['SA_CSVTRANSACTIONS'] = array(SS_IMPORTTRANSACTIONS|101, _("Import Transactions"));

		return array($security_areas, $security_sections);
	}
}
?>
