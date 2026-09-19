<?php
/**********************************************************************
    Copyright (C) FrontAccounting, LLC.
	Released under the terms of the GNU General Public License, GPL,
	as published by the Free Software Foundation, either version 3
	of the License, or (at your option) any later version.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
    See the License here <http://www.gnu.org/licenses/gpl-3.0.html>.
***********************************************************************/

include_once(__DIR__ . "/../../../sales/includes/cart_class.inc");

/**
 * The cart that carries one imported sales order / invoice.
 *
 * Earlier versions of this module carried a 700 line copy of FrontAccounting's Cart class that had to be kept in
 * step with every FrontAccounting release (it had already drifted from 2.3 to 2.4). The import only ever starts a
 * new document (trans_no = 0), which is exactly what FrontAccounting's own Cart does, so the module now uses it as is.
 */
final class import_sales_cart extends Cart
{
}
