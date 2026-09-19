<?php
//module/import_transactions: the form the user sees
include_once(__DIR__ . "/import_transactions.inc");

/**
 * The posted value of a form field for a FrontAccounting list; null when nothing was posted so that the list falls
 * back to its own default.
 */
function import_post_selected(string $key): ?string
{
    $value = import_post($key);
    return $value === '' ? null : $value;
}

//Note: Ajax will be implemented here since default is true
/**
 * @param string|array<int, string> $label a heading, or a list of lines shown as bullets
 */
function checkbox_row($label, string $name, string $value, bool $submit_on_change, string $title, string $params = ""): void //adapted from check_row to accomodate colspan
{
    if (is_array($label))
    {
        echo "<tr><td class='label' $params><ul>";
        foreach ($label as $item) {
            echo "<li>$item</li>";
        }
        echo "</ul></td>";
    }
    else
    {
        echo "<tr><td class='label' $params>$label</td>";
    }
    echo "<td>";
    echo checkbox("", $name, $value, true, $title);
    echo "</td>";
    echo "</tr>\n";
}

function import_type_list_row(string $label, string $name, string $selected): void
{
    $arr = array(ST_JOURNAL=> _("General Journal Processing"),
        ST_BANKDEPOSIT=> _("Deposit Processing"),
        ST_BANKPAYMENT=> _("Payment Processing"),ST_SALESORDER=>_("Sales Order Processing"),ST_SALESINVOICE=> _("Direct Sales Invoice Processing ")
    );
    echo "<tr><td class='label' colspan=1>$label</td><td>";
    echo array_selector($name, $selected, $arr,
        array(
            'select_submit'=> true,
            'async' => false,
        ));
    echo "</td></tr>\n";
}

function initialize_controls(): void
{
    initialize_main_controls();
    initialize_tools_controls();
}

/**
 * Gives every form field the importer reads a value, so the fields exist on the first visit.
 */
function initialize_main_controls(): void
//initialize dropdown boxes
{
    $myrow = get_company_prefs();
    if (!isset($_POST['debtors_act'])) $_POST['debtors_act'] = (string)$myrow["debtors_act"];
    if (!isset($_POST['creditors_act'])) $_POST['creditors_act'] = (string)$myrow["creditors_act"];
    if (!isset($_POST['type'])) $_POST['type'] = (string)ST_JOURNAL; // the first entry of the list
    foreach (array('stateformat', 'cashorcredit', 'salestools', 'tools', 'lookup', 'notes') as $field) {
        if (!isset($_POST[$field])) $_POST[$field] = '';
    }
    if (!isset($_POST['sep'])) $_POST['sep'] = ",";
    if (!isset($_POST['bank_account'])) $_POST['bank_account'] = _("Current account");
    if (!isset($_POST['trial'])) $_POST['trial'] = '1';
}

function initialize_tools_controls(): void
{
    foreach (array('cust', 'supp', 'fis', 'ttype', 'payment', 'payment2') as $field) {
        if (!isset($_POST[$field])) $_POST[$field] = '';
    }
    if (!isset($_POST['lookrefs'])) $_POST['lookrefs'] = _("Select a reference");
}

function show_table_section_bank_account_number(): void
{
    $account = get_bank_gl_account(import_post('bank_account'));
    table_section_title(_("Bank Accounts"),2);
    label_row(_("Account number: "), $account,'colspan=1');
}

function show_table_section_company_settings_lookup(): void
{
    table_section_title(_("Lookup: Company Settings"),2);
    echo "<tr class='oddrow'>";gl_all_accounts_list_cells(_("General Ledger Account Lookup:"), 'gl_lookup', null, false, false, _("All Accounts"), false);echo "</tr>";
    echo "<tr class='oddrow'>";fiscalyears_list_cells(_("Active Fiscal year(s):"),'fis',null);echo "</tr>";
    echo "<tr class='evenrow'>";dateformats_list_row(_("User selected Display format:"), "date_format", (string)user_date_format());echo "</tr>";
    table_section_title("");
}

function show_table_section_csv_separator(): void
{
    table_section_title(_("CSV fields are separated by: "),2);
    text_cells(_("Field Separator:"), 'sep', import_post('sep'), 2, 1, "", 'colspan=1');
}

function show_table_section_control_accounts(): void
{
    table_section_title(_("Control Accounts"),2);
    echo "<tr>";gl_all_accounts_list_cells(_("Receivable Account / Debtors Control account Default:"), 'debtors_act', import_post('debtors_act'));echo "</tr>";
    echo "<tr>";gl_all_accounts_list_cells(_("Payable Account / Creditors Control account Default:"), 'creditors_act', import_post('creditors_act'));echo "</tr>";
}

function show_table_section_display(int $type): void
{
    /** @var Ajax $Ajax */
    global $Ajax;
    table_section_title(_("Display"),2);
    if ($type == ST_SALESORDER || $type == ST_SALESINVOICE)
    {
        checkbox_row(_("Lookup id's for Payment terms relating to Sales Orders / Invoices "), 'salestools', import_post('salestools'), true, "", 'colspan=1');
        if (import_post('salestools') !== '') {show_table_section_sales();} else $Ajax->activate('_page_body');
    }
    checkbox_row(_("Lookup id's for Tax Type, Customers, and Suppliers"), 'tools', import_post('tools'), true, "", 'colspan=1');
    if (import_post('tools') !== '') {show_table_section_tools();} else $Ajax->activate('_page_body');
    checkbox_row(_("Display Company Settings Lookup"), 'lookup', import_post('lookup'), true, "", 'colspan=1');
    if (import_post('lookup') !== '') {show_table_section_company_settings_lookup();} else $Ajax->activate('_page_body');
    checkbox_row(_("Display Help Notes"), 'notes', import_post('notes'), true, "", 'colspan=1');
    if (import_post('notes') !== '') {show_table_section_notes($type);} else $Ajax->activate('_page_body');
}

/**
 * @return int the selected import type
 */
function show_table_section_import_settings(): int
{
    table_section_title(_("Import Settings"),2);
    $type = (int)import_post('type');
    import_type_list_row(_("Import Type:"), 'type', (string)$type);
    if (($type == ST_BANKPAYMENT) || ($type == ST_BANKDEPOSIT))
    {
        bank_accounts_list_cells($type == ST_BANKPAYMENT ? _("From:") : _("To:"), 'bank_account', "", true);
        show_table_section_bank_account_number();
    }
    return $type;
}

function show_table_section_important_bank(): void
{
    table_section_title(_("Display Help Notes: Important regarding bank transactions"),2);
    label_row(_("1.No negative amounts. Payment amounts are converted to negative amounts for the table 'bank_trans' which represent credit amounts."),"",'colspan=1');
    label_row(_("2.Tax inclusive expenses (eg.rent) or tax inclusive income (eg. sales) must include a taxtype if a company is registered for VAT."),"",'colspan=1');
    label_row(_("3.Dim1_ref is a description for the dimension id. "),"",'colspan=1');
    label_row(_("4.Non-essential fields: dim1_ref, dim2_ref. Include as column headers in csv even if not used."),"",'colspan=1');
    label_row(_("5.The reference field is stored in a separate database 'refs' and should be unique to every transaction."),"",'colspan=1');
    label_row(_("6. Ensure that your csv file date format includes a 4 number year ie. YYYY. Use a low-level format editor to confirm this since format can be lost when saving to csv format. So check your csv with Notepad+ for a four digit year."),"",'colspan=1');
    label_row(_("7. Ensure that 'Account code' in your spreadsheet is in text format since eg. 0010 will lose its zeros in number format when saving to csv."),"",'colspan=1');
    label_row(_("8. Template: <a href='./templates/payment_en_GB.csv'>payment_en_GB.csv using British Chart of Accounts</a> for Payment only processing. Template: <a href='./templates/deposit_en_GB.csv'>deposit_en_GB.csv using British Chart of Accounts </a> for Deposit only processing.  Template: <a href='./templates/bank_en_GB.csv'>bank_en_GB.csv using British Chart of Accounts</a> for Bank Statement format processing."),"",'colspan=1');
}

function show_table_section_important_journals(): void
{
    table_section_title(_("Display Help Notes: Important regarding Journals"),2);
    label_row(_("1. Credit amounts must be shown with a negative amount."),"",'colspan=1');
    label_row(_("2. Tax inclusive expenses (eg.rent) or tax inclusive income (eg. sales) must include a taxtype, normally 1, if a company is registered for VAT and has set up various tax types."),"",'colspan=1');
    label_row(_("3. No bank transactions are to be processed through the journal. Use 'Deposit Processing' or 'Payment Processing' for this purpose."),"",'colspan=1');
    label_row(_("4. Ensure that your csv file date format eg. DD/MM/YYYY includes a 4 number year ie. YYYY, and it complies with your User selected date format, eg. DDMMYYYY as indicated above under tools. Do this preferably with a low-level format editor since format can be lost when converting to csv from a spreadsheet package. So check your csv with Notepad+ for a four digit year."),"",'colspan=1');
    label_row(_("5. Ensure that 'Account code' in your spreadsheet is in text format since eg. 0010 will lose its zeros in number format when saving to csv."),"",'colspan=1');
    label_row(_("6. Use Template: <a href='./templates/journal_en_GB.csv'>journal_en_GB.csv using British Chart of Accounts</a>"),"",'colspan=1');
    label_row(_("7. The correct debtors control account (eg. 1100) or creditors control account (eg. 2100) should be used with Person_type_id and Person_id. Use 'Display Tax Type, Customers, and Suppliers' to check Person_type_id and Person_id."),"",'colspan=1');
    label_row(_("8. Only General Ledger accounts should be used here. Individual customers and suppliers accounts under their respective accounts are not updated with this extension."),"",'colspan=1');
    label_row(_("9. Use the 'Sales Order Processing / Direct Sales Invoice Processing' to import individual orders/invoices relative to customers and suppliers."),"",'colspan=1');
}

function show_table_section_notes(int $type): void
{
    if ($type == ST_JOURNAL)
    {
        show_table_section_important_journals();
    }
    if (($type == ST_BANKPAYMENT) || ($type == ST_BANKDEPOSIT))
    {
        show_table_section_important_bank();
    }

    if (($type == ST_SALESORDER) || ($type == ST_SALESINVOICE))
    {
        show_table_section_sales_notes();
    }
}

function show_table_section_sales_notes(): void
{
    table_section_title(_("Display Help Notes: Important regarding Importing Sales Documents"),2);
    label_row(_("1. Use Template: <a href='./templates/salesinvoices_two_invoices_multiitems_cash_sale.csv'>Import Sales Orders / Invoices</a> to import Sales Orders/Invoices from other packages."),"",'colspan=1');
    label_row(_("2. Import multi-line, and single-line sales orders, and direct invoices."),"",'colspan=1');
    label_row(_("3. Use the lookup tool for payment terms located under Display. eg. Payment term id for cash sales is 4."),"",'colspan=1');
    label_row(_("4. Credit sales invoices normally have 1 of 3 payment term id's. eg. Payment term id of 1 for 'Due 15th Of the Following Month' "),"",'colspan=1');
    label_row(_("5. Ensure that your item_code, eg. 001, for Services in your spreadsheet has a 'Text' column format otherwise you will lose your leading zeros when saving your to csv. "),"",'colspan=1');
    label_row(_("6. The reference column with, eg. T45, or simply a number eg. 5, is very important since it defines the start and finish of a source document in the spreadsheet."),"",'colspan=1');
    label_row(_("7. Ensure that you have a delivery address completed."),"",'colspan=1');
    label_row(_("8. Sales orders and delivery notes are automatically created if 'Direct Sales Invoice Processing' is selected."),"",'colspan=1');
    label_row(_("9. If the Payment id column has a value of 4 in it, ie. Cash Only,  this will automatically allocate a Customer Payment to the invoice from the Petty Cash Account."),"",'colspan=1');
}

function show_table_section_sales(): void
{
    table_section_title(_("Lookup: Orders / Invoices"),2);
    checkbox_row(_("Cash Only (no tick)/Credit Sales (tick)"), 'cashorcredit', import_post('cashorcredit'), true, "", 'colspan=1');
    $paymcat = import_post('cashorcredit') !== '' ? PM_CREDIT : PM_CASH;
    echo "<tr class='evenrow'>";sale_payment_list_cells(_("Payment terms:"), 'payment', $paymcat, null, true);echo "</tr>";
    echo "<tr class='oddrow'>";label_cell(_("Payment id: (Use this in the template provided under 'Display Help Notes'"),'colspan=1');label_cell(import_post('payment'),'colspan=1 align=right width=20px');echo "</tr>";
    table_section_title("");
}

function show_table_section_tools(): void
{
    table_section_title(_("Lookup: Tax Type, Customers, and Suppliers"),2);
    echo "<tr class='evenrow'>";tax_types_list_cells(_("Tax Type:"),'ttype',import_post_selected('ttype'),_("All Tax types"),true);echo "</tr>";
    $ttype = import_post('ttype');
    if ($ttype === '-1') $ttype = '';
    echo "<tr class='oddrow'>";label_cell(_("Taxtype id:"),'colspan=1');label_cell($ttype,'colspan=1 align=right width=20px');echo "</tr>";
    echo "<tr class='evenrow'>";customer_list_cells(_("Customer:"),'cust',import_post_selected('cust'),_("All Customers"),true);echo "</tr>";
    echo "<tr class='oddrow'>";label_cell(_("Person_id:"),'colspan=1');label_cell(import_post('cust'),'colspan=1 align=right width=20px');echo "</tr>";
    echo "<tr class='evenrow'>";supplier_list_cells(_("Supplier:"),'supp',import_post_selected('supp'),_("All Suppliers"),true);echo "</tr>";
    echo "<tr class='oddrow'>";label_cell(_("Person_id:"),'colspan=1');label_cell(import_post('supp'),'colspan=1 align=right width=20px');echo "</tr>";
    table_section_title("");
}

function show_table_section_trial_or_final(): void
{
    table_section_title(_("Trial-run (tick) or Final-run (no tick)"),2);
    $_POST['trial'] = '1'; // always offer a trial run first
    check_row(_("Trial-run before importing. Do NOT process into database."), 'trial', '1', false, "");
    label_row(_("Import File:"), "<input type='file' id='imp' name='imp'>",'colspan=1');
}

function show_table_section_bankstatement_checkbox(): void
{
    $bs = array(    _("First amount column (col D) in csv = Payments"),
                    _("Second amount column (col E) in csv = Deposits"),
                    _("The same csv file should be used for both 'Payment' and 'Deposit' processing."),
                    _("When you select 'Deposit Processing' then all csv lines with an amount in the Payments column D will be ignored."),
                    _("When you select 'Payment Processing' then all csv lines with an amount in the Deposits column E will be ignored."),
                    _("Typically you will import a Bank statement in csv format from your bank and adjust it according to required columns."),
                    _("Bank statement = columns B,C,D,E"),
                    _("Add columns A (reference), and columns F,G,H,I,J,K. See template."),
                    _("You will need to process bank.csv twice. Once with 'Payment Processing', next with 'Deposit Processing'"),
                    _("Template <a href='./templates/bank_en_GB.csv'>bank_en_GB.csv</a>."));
    table_section_title(_("Bank Statement Format"),2);
    checkbox_row($bs, 'stateformat', import_post('stateformat'), false, "", 'colspan=1 wrap align=left');
}
