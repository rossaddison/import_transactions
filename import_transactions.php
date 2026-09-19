<?php

//Author: Ross Addison Version 2.3.21 upgrade to Tom Hallman's 2.3.3
//Website: http://www.bbqq.co.uk
//Email: frontaccounting@bbqq.co.uk
//New features:
//1. Trial check before importing.
//2. Tabular display of journal entries, each journal grouped into  debits and credits using Front Accounting's 'items cart' class.
//3. Importing of bank statements.
//4. Tax type inclusion for VAT registered companies.
//5. Inclusion of transactions automatically in an audit trail.
//6. Display notifications identifying how tables within the database are being affected for a more transparent display to interested programmers.
//7. Additional lookup tools for looking up customer, supplier, company setup information eg. fiscal year, and other tools that users might find useful for their import.
//8. Inclusion of csv examples using the en_GB account structure under folder templates.
//9. See the Spreadsheet_headers file for csv formats.
//10. Import Sales Orders/Invoices

//Module contents
//./Import_transactions/import_transactions.php
//./Import_transactions/template/bank_en_GB.csv                                       .......for bank statement processing......users process bank.csv twice. once with 'deposit processing' and once with 'payment processing'.
//./Import_transactions/template/deposit_en_GB.csv                                    .......for non-bank statement deposit processing...deposit.csv
//./Import_transactions/template/payment_en_GB.csv                                    .......for non-bank statement payment processing...payment.csv
//./Import_transactions/template/journal_en_GB.csv                                    .......for non-bank journal processing.............journal.csv
//./Import_transactions/includes/import_transactions.inc                              .......functions that post and check the imported lines.
//./Import_transactions/includes/import_transactions_ui.inc                           .......the form.
//./Import_transactions/includes/import_rows.inc                                      .......typed csv rows.
//./Import_transactions/includes/import_sales_order_entry.inc                         .......for importing sales orders/invoices
//./Import_transactions/includes/import_sales_cart_class.inc
//./Import_transactions/includes/import_sales_order_ui.inc

$page_security = 'SA_CSVTRANSACTIONS';
$path_to_root="../..";
include_once(__DIR__ . "/../../includes/session.inc");
include_once(__DIR__ . "/../../includes/sysnames.inc"); //referencetype_list_row for determining next reference for source documents
include_once(__DIR__ . "/../../includes/main.inc"); //function page
include_once(__DIR__ . "/../../includes/ui.inc");
include_once(__DIR__ . "/../../includes/ui/items_cart.inc"); //class 'items_cart' gl_items for classic tabular representation of journals
include_once(__DIR__ . "/../../includes/ui/ui_input.inc");
include_once(__DIR__ . "/../../includes/references.inc"); //get next reference, exists reference.
include_once(__DIR__ . "/../../includes/db/audit_trail_db.inc"); //add_audit_trail mandatory for all import transactions
// FA 2.4 merged the references_db.inc functions into includes/references.inc (included above).
include_once(__DIR__ . "/../../gl/includes/db/gl_db_trans.inc"); // write journal entries; add_gl_tax_details; add_gl_trans
include_once(__DIR__ . "/../../gl/includes/db/gl_db_bank_trans.inc"); //add_bank_trans
include_once(__DIR__ . "/../../gl/includes/db/gl_db_bank_accounts.inc"); //get_bank_gl_account
include_once(__DIR__ . "/../../gl/includes/db/gl_db_accounts.inc"); // gl_account_in_bank_accounts
include_once(__DIR__ . "/../../gl/includes/gl_db.inc"); //link to other includes
include_once(__DIR__ . "/../../dimensions/includes/dimensions_db.inc"); //get_dimension_string
include_once(__DIR__ . "/../../includes/date_functions.inc"); //sql2date, is_date_in_fiscalyear
include_once(__DIR__ . "/../../includes/data_checks.inc");
include_once(__DIR__ . "/../../admin/db/company_db.inc"); //default control accounts
include_once(__DIR__ . "/../../includes/ui/ui_controls.inc");
include_once(__DIR__ . "/includes/import_transactions.inc"); //functions used
include_once(__DIR__ . "/includes/import_transactions_ui.inc"); //the form
include_once(__DIR__ . "/includes/import_sales_order_entry.inc"); // adaptation of sales_order_entry.php
include_once(__DIR__ . "/includes/import_sales_cart_class.inc"); // adaptation of cart class
include_once(__DIR__ . "/includes/import_sales_order_ui.inc"); // adaptation of sales_order_ui.inc

add_access_extensions();

//Turn these next two lines on for debugging. They are off by default: FrontAccounting 2.4 only collects and
//displays messages (notifications, errors) while error_reporting() is the level it set itself, so forcing
//E_ALL here made every message of this page disappear.
//error_reporting(E_ALL);
//ini_set("display_errors", "on");

/**
 * What a run over the csv lines counted.
 */
final class import_run
{
    /** @var int lines that had at least one error */
    public $errCnt = 0;
    /** @var int lines that were processed without error */
    public $entryCount = 0;
    /** @var int documents (sales orders / invoices) started */
    public $doc_num = 0;
    /** @var bool */
    public $displayed_at_least_once = false;
}

/**
 * Finishes the sales order / invoice that is being collected.
 */
function import_write_document(?Cart $cart): void
{
    if ($cart !== null) {
        $cart->write(0);
        $cart->clear_items();
    }
}

/**
 * Processes the csv lines of an uploaded file.
 *
 * @param resource $fp the open csv file
 */
function import_process_lines($fp, int $type, string $sep, bool $stateformat, string $bank_account, string $bank_account_gl_code, import_run $run): void
{
    /** @var references $Refs */
    global $Refs;

    $file = import_file_name();
    $entry = new items_cart($type);
    init_entry_part_1($entry);
    $curEntryId = last_transno($type)+1;
    $line = 0;
    $description = "";
    $i = 0;
    $docline = 1;
    $firstlinecopied = false;
    $total_debit_positive = 0.0;
    $total_credit_negative = 0.0;
    $prev_ref = null;
    $prev_date = null;
    $isSales = ($type == ST_SALESORDER) || ($type == ST_SALESINVOICE);
    $isGl = ($type == ST_BANKDEPOSIT) || ($type == ST_BANKPAYMENT) || ($type == ST_JOURNAL);
    $isBank = ($type == ST_BANKDEPOSIT) || ($type == ST_BANKPAYMENT);
    $cart = null;
    $error = false;
    $reference = '';
    $date = '';
    check_db_has_stock_items(_("There are no inventory items defined in the system."));
    check_db_has_customer_branches(_("There are no customers, or there are no customers with branches. Please define customers and customer branches."));
    while (is_array($data = fgetcsv($fp, 4096, $sep, '"', '\\')))
    {
        $line++;
        if ($line == 1)
        {
            display_notification_centered(_("Skipped header. (line $line in import file '$file')"));
            continue;
        }
        display_notification_centered(" --------------------------------------------------------------------------------------------Line $line ------------------------------------------------------------------------------------------");

        $l = null;
        $s = null;
        if ($isSales)
        {
            $s = import_sales_line::fromCsv($data);
            $reference = $s->reference;
            $date = $s->date;
        }
        else
        {
            $l = import_line::fromCsv($data, $type, $stateformat);
            $reference = $l->reference;
            $date = $l->date;
            //A bank statement carries both a payment and a receipt column; each type of processing takes its own.
            if ($stateformat && $type == ST_BANKPAYMENT && !(($l->ignore === '') && ($l->amt > 0.01)))
            {
                display_notification_centered(_("Ignoring deposit. Use same csv under deposit processing. (line $line in import file '$file')"));
                $error = false;
                $prev_ref = $reference;
                continue;
            }
            if ($stateformat && $type == ST_BANKDEPOSIT && !(($l->ignore === '') && ($l->amt > 0.01)))
            {
                display_notification_centered(_("Ignoring payment. Use same csv under payment processing.(line $line in import file '$file')"));
                $error = false;
                $prev_ref = $reference;
                continue;
            }
        }

        if ($s !== null)
        {
            display_notification_centered(_("Processing line $line ({$s->summary}) in import file '$file')"));
            if (!customer_exist($s->customer_id)) {
                display_notification("Customer does not exist in the database");
                $error = true;
            }
            if ($cart === null || $prev_ref !== $reference) // reference has changed so new invoice with new lineitem(s)
            {
                if ($firstlinecopied) // if the reference has changed for line items write the preference reference based document to the cart
                {
                    import_write_document($cart);
                    $firstlinecopied = false;
                }
                $docline = 1; //the reference has changed so this will be the first line item.
                $run->doc_num++;
                $cart = new import_sales_cart($type, 0, false);
                $cart->document_date = $date;
                $cart->order_no = $reference; //order_no is the source document's original
            }
            else
            {
                $docline = $docline + 1;
            }
            $com = get_customer_details_to_order($cart, $s->customer_id, $s->branch_no);
            display_notification_centered($com);
            if ($com != "")
            {
                display_notification_centered("Error");
                $error = true;
            }
            copy_to_cart($cart, $s);
            $firstlinecopied = true;
            if (!import_add_to_order($cart, $s->item_code, (float)$s->quantity, (float)$s->price, (float)$s->discountpercentage, $s->item_description)) {
                $error = true;
            }
            $cart->cust_ref = $reference;
            if ((!check_import_item_data($docline, $s, $cart)) || (!can_process($line, $s, $cart)))
            {
                display_notification_centered("Error");
                $error = true;
            }
        }

        if (($prev_ref !== $reference) && ($type < 4)) {
            init_entry_part_2($entry, $date, $reference);
        }

        $dim1 = 0;
        $dim2 = 0;
        if ($l !== null)
        {
            if ($type == ST_JOURNAL)
            {
                list($error, $complete, $total_debit_positive, $total_credit_negative) = journal_id($prev_date, $date, $l->amt, $total_debit_positive, $total_credit_negative, $line);
            }
            else
            {
                $complete = true;
            }

            $error = check_customer_supplier($l, $line, $error);
            $code_exists = check_code_id($l->code_id);
            if (!$code_exists) {
                display_notification_centered("Error: Account code {$l->code_id} does not exist");
                $error = true;
            }
            $dim1found = get_dimension_id_from_dimref($l->dim1_ref);
            if ($dim1found === null) {
                display_error(_("Error: Could not find dimension with dimension reference '{$l->dim1_ref}' (line $line in import file '$file')"));
                $error = true;
            }
            $dim1 = $dim1found === null ? 0 : $dim1found;
            $dim2found = get_dimension_id_from_dimref($l->dim2_ref);
            if ($dim2found === null) {
                display_error(_("Error: Could not find dimension with dimension reference '{$l->dim2_ref}' (line $line in import file '$file')"));
                $error = true;
            }
            $dim2 = $dim2found === null ? 0 : $dim2found;
            $description = $code_exists ? get_gl_account_name($l->code_id) : _("Unknown account");
        }
        else
        {
            $complete = true;
        }

        if ($reference == '') {
            display_error(_("$line does not have a reference. (line $line in import file '$file')"));
            $error = true;
        }
        $reference_is_new = $Refs->is_new_reference($reference, $type);
        if ((!$reference_is_new) && ($reference !== $prev_ref)) {
            display_error(_("Error: Reference from table 'refs': '$reference' is already in use. (line $line in import file '$file')"));
            $error = true;
        } elseif ($reference_is_new && ($reference !== $prev_ref)) {
            $Refs->save($type, $curEntryId, $reference);
        }

        if (!is_date($date)) {
            display_error(_("Error: date '$date' not properly formatted (line $line in import file '$file')"));
            $error = true;
        }
        if (!is_date_in_fiscalyear($date)) {
            display_error(_("Error: Date not within company fiscal year. Make sure date is in dd/mm/yyyy format and your csv years are 4 digits long. Check that current fiscal year is active under Setup..Company Setup"));
            $error = true;
        }

        if ($l !== null)
        {
            $bankdesc = $isBank ? get_gl_account_name($bank_account_gl_code) : "";
            $i = journal_display($i, $type, $l->taxtype, $l->amt, $entry, $l->code_id, $dim1, $dim2, $l->memo, $description, $bank_account_gl_code, $bankdesc);
        }

        if (!$error)
        {
            if ($l !== null)
            {
                if ($type == ST_JOURNAL)
                {
                    if (gl_account_in_bank_accounts($l->code_id)) {
                        display_notification_centered(_("Error: Bank account detected in journal. No processing of bank accounts allowed. (line $line in import file '$file')"));
                        $error = true;
                    }
                    if (check_tax_appropriate($l->code_id, $l->taxtype, $line))
                    {
                        journal_inclusive_tax($type, $line, $curEntryId, $l, $dim1, $dim2);
                        add_audit_trail($type, $curEntryId, $date);
                    }
                }
                elseif ($isBank && ($l->amt > 0))
                {
                    if (check_tax_appropriate($l->code_id, $l->taxtype, $line))
                    {
                        bank_inclusive_tax($type, $bank_account, $bank_account_gl_code, $line, $curEntryId, $l, $dim1, $dim2);
                    }
                    else
                    {
                        display_notification_centered(_("Warning: Taxtype used with Asset or Liability - $curEntryId, $date, {$l->code_id}.(line $line in import file '$file')"));
                    }
                }
                elseif ($isBank && ($l->amt < 0))
                {
                    display_notification_centered(_("Error: Credit amounts represented by negative amounts being entered. Check csv file is correct.(line $line in import file '$file')"));
                    $error = true;
                }
            }
            $run->entryCount = $run->entryCount + 1;
        }

        if ($error) {
            $run->errCnt = $run->errCnt + 1;
        }
        $error = false;
        $prev_ref = $reference;
        $prev_date = $date;
        //every line has its own number, except that the lines of one journal share a number
        $curEntryId += (($type != ST_JOURNAL) || $complete) ? 1 : 0;
    }//while

    if ($isGl)
    {
        $run->displayed_at_least_once = display_entries($type, $entry);
        end_row();
        end_table(1);
        div_end();
        // a journal that was still open at the end of the file has debits that do not equal its credits
        $open_journal = ($type == ST_JOURNAL) && ($total_debit_positive != 0.0 || $total_credit_negative != 0.0);
        if (!$run->displayed_at_least_once || $open_journal) //there has been no occurance of debits equaling credits - at least one journal not properly balanced
        {
            display_notification_centered(_("Error: Debits do not equal credits."));
            $run->errCnt = $run->errCnt + 1;
        }
    }

    if ($isSales && $firstlinecopied && $prev_ref === $reference) //for the last line item in a csv
    {
        import_write_document($cart);
    }
}

/**
 * Imports the uploaded csv file: trial run or final run according to the form.
 */
function import_process_upload(int $type): void
{
    $filename = isset($_FILES['imp']['tmp_name']) ? $_FILES['imp']['tmp_name'] : '';
    if (import_file_name() == '' || $filename == '')
    {
        return;
    }
    $stateformat = ($type > 0) && isset($_POST['stateformat']);
    $sep = import_post('sep');
    $bank_account = import_post('bank_account');
    $bank_account_gl_code = $bank_account !== '' ? get_bank_gl_account($bank_account) : "";
    $fp = @fopen($filename, "r");
    if (!$fp)
    {
        display_error(_("Error opening file $filename"));
        return;
    }
    if (strlen($sep) != 1)
    {
        display_error(_("The csv field separator must be exactly one character."));
        fclose($fp);
        return;
    }
    begin_transaction();
    $run = new import_run();
    import_process_lines($fp, $type, $sep, $stateformat, $bank_account, $bank_account_gl_code, $run);
    fclose($fp);

    // Commit import to database
    $trial_text = import_post('trial');
    $trial = $trial_text !== '' && $trial_text !== '0';

    $doc_num = $run->doc_num;
    if ($type == ST_JOURNAL) {$typeString = "General Journals";}
    elseif ($type == ST_BANKDEPOSIT) {$typeString = "Deposits";}
    elseif ($type == ST_BANKPAYMENT) {$typeString = "Payments";}
    elseif ($type == ST_SALESORDER) {$typeString = "Sales Order csv lines / $doc_num order(s)";}
    elseif ($type == ST_SALESINVOICE) {$typeString = "Sales Invoices csv lines / $doc_num invoice(s)";}
    else {$typeString = "";}

    $entryCount = $run->entryCount;
    $errCnt = $run->errCnt;
    if (!$trial) {
        if ($errCnt == 0) {
            if ($entryCount > 0) {
                commit_transaction();
                display_notification_centered(_("$entryCount $typeString have been imported."));
            } else display_error(_("Import file contained no $typeString."));
        }
    } else {
        if ($errCnt == 0) {
            if ($entryCount > 0) display_notification_centered(_("$entryCount $typeString would have been successful if imported. Uncheck Trial check before importing."));
            else display_notification_centered(_("Import file contained no $typeString. Populate file with data before importing."));
        }
        if (($errCnt > 0) && $run->displayed_at_least_once) display_notification_centered(_("$errCnt error(s) detected. Correct before importing."));
    }
}

/**
 * The import page: processes an uploaded file, then shows the form.
 */
function import_transactions_page(): void
{
    /** @var sys_prefs $SysPrefs */
    global $SysPrefs;

    //Set '$yes' to true if you are testing this module and you do not want to manually(phpmyadmin) delete previous test run records before each test run
    //Ensure that your company has no important information in it as these will be deleted by means of all_delete function under import_transactions.inc
    //Warning: Most records will be deleted if '$yes' set to true. Default must stay on false for normal operation.
    //Recommended: Remove this next line after you are happy with testing.
    all_delete(false);

    $js = '';
    if ($SysPrefs->use_popup_windows) {$js .= get_js_open_window(800, 500);}
    $help_context = "Import General Journals  / Deposits / Payments / Bank Statements / Sales Orders / Sales Invoices  <a href='spreadsheet_headers.html'>Help: Formats</a>";
    page(_($help_context), false, false, "", $js);

    if (isset($_POST['type']))
    {
        import_process_upload((int)import_post('type'));
    }

    // User Interface
    start_form(true);
    div_start('_main_table');
    initialize_controls();
    start_table(TABLESTYLE2,"width=95%");//inner table
    $type = show_table_section_import_settings();
    if (($type == ST_JOURNAL) || ($type == ST_BANKDEPOSIT) || ($type == ST_BANKPAYMENT))
    {
        show_table_section_control_accounts();
    }
    show_table_section_display($type);
    if (($type == ST_BANKDEPOSIT) || ($type == ST_BANKPAYMENT))
    {
        show_table_section_bankstatement_checkbox();
    }
    show_table_section_csv_separator();
    show_table_section_trial_or_final();
    end_table(1);
    div_end('_main_table');
    submit_center('import', "Process", true, false, true, ICON_OK);
    end_form();
    end_page();
}

import_transactions_page();
